/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "viewer.h"
#include "psmtp/copy_paths.h"
#include "psmtp/metadata.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define TEXT_LIMIT (256u*1024u)
#define IMAGE_LIMIT (64u*1024u*1024u)

int psmtp_viewer_kind(const char *name) {
    const char *extension=strrchr(name,'.');
    static const char *const types[][22]={
        {".txt",".ini",".cfg",".conf",".log",".md",".json",".xml",".csv",".tsv",".yaml",".yml",".html",".css",".js",".lua",".py",".sh",".c",".h",".srt",NULL},
        {".jpg",".jpeg",".png",".bmp",".gif",".tga",NULL},
        {".mp4",".m4v",".mov",".mkv",".webm",".avi",".ts",".m2ts",".mpg",".mpeg",NULL},
        {".mp3",".m4a",".aac",".wav",".flac",".ogg",".opus",NULL}
    };
    if(!extension)return PSMTP_FILE_UNKNOWN;
    for(size_t i=0;i<sizeof(types)/sizeof(types[0]);++i)
        for(size_t j=0;types[i][j];++j)if(!strcasecmp(extension,types[i][j]))return (int)i+1;
    return PSMTP_FILE_UNKNOWN;
}

void psmtp_unstage_file(const char *directory,const char *path) {
    if(*path)unlink(path);
    if(*directory)rmdir(directory);
}

int psmtp_stage_file(const psmtp_op_ref *source,mtp_client *client,char directory[PSMTP_LOCAL_PATH],char path[PSMTP_LOCAL_PATH],psmtp_work *work) {
    psmtp_op_plan *plan=calloc(1,sizeof(*plan));
    psmtp_location internal={.kind=PSMTP_LOCATION_INTERNAL,.root="/data"};
    FILE *log=psmtp_work_log(work);
    if(!plan)return MTP_MEMORY;
    int rc=psmtp_local_prepare_copies(log);
    if(!rc){
        strcpy(directory,PSMTP_COPY_DIRECTORY "/.viewer-XXXXXXXX");
        if(!mkdtemp(directory)){directory[0]=0;rc=MTP_FILESYSTEM;}
    }
    int media=psmtp_viewer_kind(source->name)==PSMTP_FILE_VIDEO || psmtp_viewer_kind(source->name)==PSMTP_FILE_MUSIC;
    if(media)psmtp_work_cancel_between_requests(work,1);
    if(!rc)rc=psmtp_op_plan_build(plan,source,client,psmtp_work_progress,work,log);
    if(!rc){
        mtp_copy_source *phone=&plan->nodes[0].phone;
        if(media){
            if(phone->partial_supported && (phone->extended || phone->size<UINT32_MAX))phone->streamed=0;
            else psmtp_work_cancel_between_requests(work,0);
        }
        const char *extension=strrchr(source->name,'.');
        snprintf(plan->nodes[0].name,sizeof(plan->nodes[0].name),"source%.24s",extension?extension:"");
        rc=psmtp_op_target(plan,&internal,directory);
    }
    if(!rc)rc=psmtp_op_space(plan,client);
    if(!rc)rc=psmtp_op_execute(plan,client,0,psmtp_work_progress,work);
    if(!rc)strcpy(path,plan->nodes[0].target);
    if(media)psmtp_work_cancel_between_requests(work,0);
    free(plan);
    return rc;
}

static int text_valid(const char *text,size_t length) {
    size_t i=0;
    while(i<length){
        unsigned c=(unsigned char)text[i++];
        if(c<128){if((c<32 && c!='\t' && c!='\r' && c!='\n') || c==127)return 0;continue;}
        size_t more=c>=0xc2 && c<=0xdf?1:c>=0xe0 && c<=0xef?2:c>=0xf0 && c<=0xf4?3:0;
        if(!more || more>length-i)return 0;
        unsigned lead=c,second=(unsigned char)text[i];
        if((lead==0xe0 && second<0xa0) || (lead==0xed && second>=0xa0) ||
           (lead==0xf0 && second<0x90) || (lead==0xf4 && second>=0x90))return 0;
        while(more--)if(((unsigned char)text[i++]&0xc0)!=0x80)return 0;
    }
    return 1;
}

int psmtp_viewer_load(void *context,psmtp_work *work) {
    psmtp_viewer *v=context;
    int fd=-1,rc;
    size_t limit=v->kind==PSMTP_FILE_TEXT?TEXT_LIMIT:v->kind==PSMTP_FILE_IMAGE?IMAGE_LIMIT:SIZE_MAX;
    if(v->client){
        if(v->source.info.size32!=UINT32_MAX && v->source.info.size32>limit)return MTP_LIMIT;
        rc=psmtp_stage_file(&v->source,v->client,v->staged_directory,v->staged_path,work);
        if(rc)return rc;
        fd=open(v->staged_path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    }else {
        rc=psmtp_op_open_read(&v->source,&fd);
        if(rc)return rc;
        v->read_only=v->source.wrapped;
    }
    if(fd<0)return MTP_FILESYSTEM;
    if(v->kind==PSMTP_FILE_VIDEO || v->kind==PSMTP_FILE_MUSIC){close(fd);return 0;}
    psmtp_native_stat stat;
    rc=psmtp_native_status(fd,&stat);
    if(rc){close(fd);return rc;}
    if(stat.st_size<0 || (uint64_t)stat.st_size>limit){close(fd);return MTP_LIMIT;}
    size_t length=(size_t)stat.st_size;
    char *bytes=malloc(length+1);
    if(!bytes){close(fd);return MTP_MEMORY;}
    for(size_t at=0;at<length;){
        if(psmtp_work_progress(work,MTP_COPY_LOCAL,at,length)){rc=MTP_CANCELLED;break;}
        size_t amount=length-at>65536?65536:length-at;
        ssize_t n=read(fd,bytes+at,amount);
        if(n<0 && errno==EINTR)continue;
        if(n<=0){rc=MTP_FILESYSTEM;break;}
        at+=(size_t)n;
    }
    if(close(fd) && !rc)rc=MTP_FILESYSTEM;
    if(!rc && v->kind==PSMTP_FILE_IMAGE)rc=psmtp_decode_still((uint8_t *)bytes,length,&v->pixels,&v->width,&v->height);
    if(!rc && v->kind==PSMTP_FILE_TEXT){
        if(!text_valid(bytes,length))rc=MTP_PROTOCOL;
        else {
            bytes[length]=0;v->text=bytes;v->length=length;v->lines=1;
            const char *newline=memchr(bytes,'\n',length);
            v->crlf=newline && newline>bytes && newline[-1]=='\r';
            for(size_t i=0;i<length;++i)if(bytes[i]=='\n')++v->lines;
            return 0;
        }
    }
    free(bytes);
    return rc;
}

void psmtp_viewer_close(psmtp_viewer *v) {
    free(v->text);free(v->pixels);
    psmtp_unstage_file(v->staged_directory,v->staged_path);
}

static void line_range(const psmtp_viewer *v,size_t line,size_t *start,size_t *end,size_t *next) {
    size_t at=v->length>=3 && !memcmp(v->text,"\xef\xbb\xbf",3)?3:0;
    while(line-- && at<v->length){while(at<v->length && v->text[at++]!='\n'){} }
    *start=at;
    while(at<v->length && v->text[at]!='\n')++at;
    *next=at<v->length?at+1:at;
    *end=at>*start && v->text[at-1]=='\r'?at-1:at;
}

size_t psmtp_viewer_line(const psmtp_viewer *v,size_t line,size_t column,char *out,size_t capacity) {
    size_t start,end,next;
    line_range(v,line,&start,&end,&next);
    if(column>end-start)column=end-start;
    start+=column;
    while(start<end && ((unsigned char)v->text[start]&0xc0)==0x80)++start;
    size_t length=end-start,copy=length<capacity?length:capacity-1;
    while(copy<length && ((unsigned char)v->text[start+copy]&0xc0)==0x80)--copy;
    memcpy(out,v->text+start,copy);out[copy]=0;
    return length;
}

void psmtp_viewer_scroll(psmtp_viewer *v,int shift){
    size_t start,end,next;
    line_range(v,v->line,&start,&end,&next);
    int column=(int)v->column+shift;
    if(column<0)column=0;
    if((size_t)column>end-start)column=(int)(end-start);
    while(column>0 && ((unsigned char)v->text[start+(size_t)column]&0xc0)==0x80)--column;
    v->column=(size_t)column;
}

int psmtp_viewer_edit(psmtp_viewer *v,const char *text,int action) {
    size_t start,end,next,length=strlen(text);
    if(v->read_only || v->line>=v->lines)return MTP_UNSUPPORTED;
    line_range(v,v->line,&start,&end,&next);
    if(action==1){
        text=v->crlf?"\r\n":"\n";length=v->crlf?2:1;
        start=end;
    }else if(action==-1){
        length=0;
        end=next;
        if((!end || v->text[end-1]!='\n') && v->line && start && v->text[start-1]=='\n'){
            --start;if(start && v->text[start-1]=='\r')--start;
        }
    }else {
        if(!text_valid(text,length) || strpbrk(text,"\r\n"))return MTP_PROTOCOL;
        start+=v->column<end-start?v->column:end-start;
        size_t segment=end-start;
        if(segment>239){segment=239;while(((unsigned char)v->text[start+segment]&0xc0)==0x80)--segment;}
        end=start+segment;
    }
    size_t total=v->length-(end-start)+length;
    if(total>TEXT_LIMIT)return MTP_LIMIT;
    if(length==end-start && !memcmp(v->text+start,text,length))return 0;
    char *updated=realloc(v->text,total+1>v->length+1?total+1:v->length+1);
    if(!updated)return MTP_MEMORY;
    v->text=updated;
    memmove(v->text+start+length,v->text+end,v->length-end+1);
    memcpy(v->text+start,text,length);
    v->length=total;v->dirty=1;
    if(action==1){++v->lines;++v->line;}
    if(action==-1 && v->lines>1){--v->lines;if(v->line>=v->lines)--v->line;}
    if(action)v->column=0;
    else psmtp_viewer_scroll(v,0);
    return 0;
}
