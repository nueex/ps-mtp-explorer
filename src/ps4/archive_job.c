/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "archive_job.h"
#include "download.h"
#include "psmtp/copy_paths.h"
#include "psmtp/name.h"
#include "optional_module.h"
#include <archive.h>
#include <archive_entry.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include "dirent_check.h"

enum {ARCHIVE_PATH_LIMIT=1024,ARCHIVE_DEPTH_LIMIT=32,ARCHIVE_ENTRY_LIMIT=16384,
      ARCHIVE_IO_BYTES=MTP_STREAM_CHUNK};

static const struct {
    const char *label,*extension,*filter;
    int (*set_format)(struct archive *);
    int (*add_filter)(struct archive *);
    unsigned min_level,max_level;
} archive_formats[PSMTP_ARCHIVE_FORMATS]={
    [PSMTP_ARCHIVE_ZIP]={"ZIP",".zip",NULL,archive_write_set_format_zip,NULL,0,9},
    [PSMTP_ARCHIVE_TAR_GZ]={"TAR.GZ",".tar.gz","gzip",archive_write_set_format_pax_restricted,archive_write_add_filter_gzip,0,9},
    [PSMTP_ARCHIVE_TAR]={"TAR",".tar",NULL,archive_write_set_format_pax_restricted,NULL,0,0},
    [PSMTP_ARCHIVE_TAR_BZ2]={"TAR.BZ2",".tar.bz2","bzip2",archive_write_set_format_pax_restricted,archive_write_add_filter_bzip2,1,9},
    /* Higher LZMA2/XZ presets require hundreds of MiB of encoder memory. */
    [PSMTP_ARCHIVE_TAR_XZ]={"TAR.XZ",".tar.xz","xz",archive_write_set_format_pax_restricted,archive_write_add_filter_xz,0,6},
    [PSMTP_ARCHIVE_TAR_ZST]={"TAR.ZST",".tar.zst","zstd",archive_write_set_format_pax_restricted,archive_write_add_filter_zstd,1,9},
    [PSMTP_ARCHIVE_7Z]={"7-Zip (.7z)",".7z",NULL,archive_write_set_format_7zip,NULL,0,6},
    [PSMTP_ARCHIVE_CPIO]={"CPIO (newc)",".cpio",NULL,archive_write_set_format_cpio_newc,NULL,0,0},
    [PSMTP_ARCHIVE_GZ]={"Gzip (.gz)",".gz","gzip",archive_write_set_format_raw,archive_write_add_filter_gzip,0,9},
    [PSMTP_ARCHIVE_BZ2]={"Bzip2 (.bz2)",".bz2","bzip2",archive_write_set_format_raw,archive_write_add_filter_bzip2,1,9},
    [PSMTP_ARCHIVE_XZ]={"XZ (.xz)",".xz","xz",archive_write_set_format_raw,archive_write_add_filter_xz,0,6},
    [PSMTP_ARCHIVE_ZST]={"Zstandard (.zst)",".zst","zstd",archive_write_set_format_raw,archive_write_add_filter_zstd,1,9},
};
const char *psmtp_archive_format_label(enum psmtp_archive_format format){return archive_formats[format].label;}
const char *psmtp_archive_extension(enum psmtp_archive_format format){return archive_formats[format].extension;}
int psmtp_archive_single_file(enum psmtp_archive_format format){return archive_formats[format].set_format==archive_write_set_format_raw;}
unsigned psmtp_archive_min_level(enum psmtp_archive_format format){return archive_formats[format].min_level;}
unsigned psmtp_archive_max_level(enum psmtp_archive_format format){return archive_formats[format].max_level;}

/* Used for the 7-Zip writer's spool and for the inner archive of an encrypted
 * wrapper. Unlink before writing so an interruption cannot leave plaintext. */
int psmtp_archive_scratch(const char *unused){
    char path[]=PSMTP_COPY_DIRECTORY "/.psmtp-archive-XXXXXXXX";
    (void)unused;
    int fd=mkstemp(path);
    if(fd<0)return -1;
    int removed=unlink(path);
    if(removed || fcntl(fd,F_SETFD,FD_CLOEXEC)<0){
        int error=errno;
        close(fd);
        if(removed)(void)unlink(path);
        errno=error;
        return -1;
    }
    return fd;
}

/* libarchive calls this when generating a WinZip AES salt. Fail closed if
 * the system entropy module cannot be acquired. */
int archive_random(void *buffer,size_t length){
    int32_t (*random_bytes)(void *,size_t)=NULL;
    int owned=0;
    if(sceSysmoduleIsLoaded(ORBIS_SYSMODULE_RANDOM)){
        if(sceSysmoduleLoadModule(ORBIS_SYSMODULE_RANDOM))return ARCHIVE_FAILED;
        owned=1;
    }
    int module=psmtp_optional_module("libSceRandom");
    int rc=psmtp_optional_symbol(module,"sceRandomGetRandomNumber",&random_bytes,sizeof(random_bytes));
    if(!rc)rc=random_bytes(buffer,length);
    if(owned)sceSysmoduleUnloadModule(ORBIS_SYSMODULE_RANDOM);
    return rc?ARCHIVE_FAILED:ARCHIVE_OK;
}

/* The selected writers share this hook with the all-format dispatcher. */
void __archive_write_entry_filetype_unsupported(struct archive *a,struct archive_entry *entry,const char *format){
    archive_set_error(a,EINVAL,"%s does not support entry type %u",format,(unsigned)archive_entry_filetype(entry));
}

const char *psmtp_archive_suffix(const char *name){
    static const char *const suffixes[]={".tar.gz",".tar.bz2",".tar.xz",".tar.zst",".tar.lzma",".tgz",".tbz2",".txz",".tzst",
        ".zip",".rar",".7z",".tar",".gz",".bz2",".xz",".zst",".lzma",".cpio"};
    size_t length=strlen(name);
    for(size_t i=0;i<sizeof(suffixes)/sizeof(suffixes[0]);++i){
        size_t n=strlen(suffixes[i]);
        if(length>n && !strcasecmp(name+length-n,suffixes[i]))return name+length-n;
    }
    return NULL;
}

void psmtp_archive_forget(char secret[256]) {
    volatile char *p=secret;
    for(size_t i=0;i<256;++i)p[i]=0;
}

static int fs_error(void){return errno==ENOSPC?MTP_NO_SPACE:errno==EEXIST?MTP_EXISTS:MTP_FILESYSTEM;}

static int archive_status(struct archive *a,const char *password){
    const char *message=archive_error_string(a);
    if(archive_errno(a)==ENOSPC)return MTP_NO_SPACE;
    if(archive_errno(a)==ECANCELED)return MTP_CANCELLED;
    if(archive_errno(a)==ENOMEM)return MTP_MEMORY;
    if(archive_errno(a)==EFBIG || archive_errno(a)==ERANGE)return MTP_LIMIT;
    if(message){
        if(strcasestr(message,"crypto codec not supported") || strcasestr(message,"unsupported encryption") ||
           (strcasestr(message,"encrypt") && (strcasestr(message,"not supported") || strcasestr(message,"unavailable"))))return PSMTP_ARCHIVE_ENCRYPTION;
        if(strstr(message,"assphrase") || strstr(message,"assword") || strstr(message,"Authentication code"))
            return *password?PSMTP_ARCHIVE_PASSWORD:PSMTP_ARCHIVE_REQUIRED;
        if(strstr(message,"unrecognized") || strstr(message,"Unrecognized"))return MTP_UNSUPPORTED;
    }
    return MTP_PROTOCOL;
}

static int write_all(int fd,const void *buffer,size_t length){
    const char *data=buffer;
    while(length){
        ssize_t n=write(fd,data,length);
        if(n<0 && errno==EINTR)continue;
        if(n<=0)return fs_error();
        length-=(size_t)n;data+=n;
    }
    return 0;
}

typedef struct {int fd;unsigned char *buffer;psmtp_work *work;} archive_input;
static la_ssize_t read_archive(struct archive *a,void *context,const void **buffer){
    archive_input *input=context;
    ssize_t n;
    *buffer=input->buffer;
    if(psmtp_work_cancelled(input->work)){
        archive_set_error(a,ECANCELED,"Archive extraction cancelled");
        return -1;
    }
    do n=read(input->fd,input->buffer,ARCHIVE_IO_BYTES);while(n<0 && errno==EINTR);
    if(n<0)archive_set_error(a,errno,"Archive read failed");
    return n;
}
static la_int64_t seek_archive(struct archive *a,void *context,la_int64_t offset,int whence){
    archive_input *input=context;
    off_t result=lseek(input->fd,(off_t)offset,whence);
    if(result<0){archive_set_error(a,errno,"Archive seek failed");return ARCHIVE_FATAL;}
    return result;
}
typedef struct {int fd;psmtp_work *work;} archive_output;
static la_ssize_t write_archive(struct archive *a,void *context,const void *buffer,size_t size){
    archive_output *output=context;
    if(psmtp_work_cancelled(output->work)){
        archive_set_error(a,ECANCELED,"Archive creation cancelled");
        return -1;
    }
    if(!write_all(output->fd,buffer,size))return (la_ssize_t)size;
    archive_set_error(a,errno,"Archive write failed");
    return -1;
}

static int temporary(const char *directory_path,char path[PSMTP_LOCAL_PATH],int directory){
    static unsigned serial;
    int fd;
    do {
        int n=snprintf(path,PSMTP_LOCAL_PATH,"%s/.psmtp-archive-%u.tmp",directory_path,++serial);
        if(n<0 || (size_t)n>=PSMTP_LOCAL_PATH){errno=ENAMETOOLONG;return -1;}
        if(directory){
            if(!mkdir(path,0700)){
                fd=open(path,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
                if(fd<0)rmdir(path);
                return fd;
            }
            fd=-1;
        }else fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
    } while(fd<0 && errno==EEXIST && serial<UINT32_MAX);
    return fd;
}

static void remove_tree(const char *path){
    DIR *dir=opendir(path);
    if(!dir)return;
    struct dirent *entry;
    while((entry=readdir(dir))){
        if(!psmtp_dirent_valid(entry))break;
        if(!strcmp(entry->d_name,".") || !strcmp(entry->d_name,".."))continue;
        char child[PSMTP_LOCAL_PATH];
        int n=snprintf(child,sizeof(child),"%s/%s",path,entry->d_name);
        if(n<0 || (size_t)n>=sizeof(child))continue;
        int fd=open(child,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        if(fd>=0){close(fd);remove_tree(child);rmdir(child);}
        else unlink(child);
    }
    closedir(dir);
}

static int publish(const char *stage,const char *name,int directory){
    int reservation=-1;
    if(directory){if(mkdir(name,0700))return fs_error();}
    else {
        reservation=open(name,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
        if(reservation<0)return fs_error();
        close(reservation);
    }
    if(rename(stage,name)){
        int rc=fs_error();if(directory)rmdir(name);else unlink(name);return rc;
    }
    return 0;
}

static int normalize_entry(const char *name,char output[ARCHIVE_PATH_LIMIT]){
    if(!name || *name=='/' || *name=='\\')return MTP_PROTOCOL;
    size_t used=0,depth=0;
    while(*name){
        const char *end=strchr(name,'/');
        size_t n=end?(size_t)(end-name):strlen(name);
        if(n==1 && *name=='.'){}
        else {
            if(!n || n>255 || name[n-1]=='.' || name[n-1]==' ' || used+n+1>=ARCHIVE_PATH_LIMIT)return MTP_PROTOCOL;
            for(size_t i=0;i<n;++i)if((unsigned char)name[i]<32 || name[i]=='\\' || name[i]==':')return MTP_PROTOCOL;
            if(++depth>ARCHIVE_DEPTH_LIMIT)return MTP_LIMIT;
            if(used)output[used++]='/';
            memcpy(output+used,name,n);used+=n;
        }
        if(!end)break;
        name=end+1;
    }
    output[used]=0;
    return 0;
}

static int open_entry(const char *root,const char *path,int directory){
    char full[PSMTP_LOCAL_PATH];
    int n=snprintf(full,sizeof(full),"%s/%s",root,path);
    if(n<0 || (size_t)n>=sizeof(full)){errno=ENAMETOOLONG;return -1;}
    char *component=full+strlen(root)+1,*slash;
    while((slash=strchr(component,'/'))){
        *slash=0;
        if(mkdir(full,0777) && errno!=EEXIST)return -1;
        int next=open(full,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        *slash='/';
        if(next<0)return -1;
        close(next);component=slash+1;
    }
    if(directory){
        if(mkdir(full,0777) && errno!=EEXIST)return -1;
        return open(full,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    }
    return open(full,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0666);
}

static int remember(char **paths,const char *path){
    uint32_t hash=2166136261u;
    for(const unsigned char *p=(const unsigned char *)path;*p;++p)hash=(hash^*p)*16777619u;
    unsigned index=hash%(2*ARCHIVE_ENTRY_LIMIT);
    while(paths[index]){
        if(!strcmp(paths[index],path))return MTP_EXISTS;
        index=(index+1)%(2*ARCHIVE_ENTRY_LIMIT);
    }
    paths[index]=strdup(path);
    return paths[index]?0:MTP_MEMORY;
}

static uint64_t consumed(struct archive *archive,uint64_t total){
    la_int64_t bytes=archive_filter_bytes(archive,-1);
    return bytes<0?0:(uint64_t)bytes>total?total:(uint64_t)bytes;
}

static int extract_entry_data(struct archive *a,struct archive_entry *entry,int fd,
                              const char *password,uint64_t total,psmtp_work *work){
    int64_t written=0;
    int size_known=archive_entry_size_is_set(entry);
    int64_t expected=archive_entry_size(entry);
    for(;;){
        const void *block;
        size_t bytes;
        la_int64_t offset=written;
        int rc=archive_read_data_block(a,&block,&bytes,&offset);
        if(rc!=ARCHIVE_OK && rc!=ARCHIVE_EOF)return archive_status(a,password);
        if(rc==ARCHIVE_EOF){
            /* A trailing sparse hole contributes to the file's logical size. */
            if(offset>written){
                if(size_known && offset!=expected)return MTP_PROTOCOL;
                if(ftruncate(fd,(off_t)offset))return fs_error();
                written=offset;
            }
            return size_known && written!=expected?MTP_PROTOCOL:0;
        }
        if(offset<written || bytes>(uint64_t)(INT64_MAX-offset) ||
           (size_known && offset+(int64_t)bytes>expected))return MTP_PROTOCOL;
        if(offset!=written && lseek(fd,(off_t)offset,SEEK_SET)!=offset)return fs_error();
        rc=write_all(fd,block,bytes);
        if(rc)return rc;
        written=offset+(int64_t)bytes;
        if(psmtp_work_progress(work,(enum mtp_copy_phase)PSMTP_OP_EXTRACT,consumed(a,total),total))
            return MTP_CANCELLED;
    }
}

static int extract_contents(psmtp_archive_job *job,int source,const char *target,uint64_t total,psmtp_work *work){
    struct archive *a=archive_read_new();
    char **paths=calloc(2*ARCHIVE_ENTRY_LIMIT,sizeof(*paths));
    archive_input input={source,malloc(ARCHIVE_IO_BYTES),work};
    int rc=MTP_MEMORY;
    if(!a || !paths || !input.buffer)goto done;
    archive_read_support_filter_gzip(a);archive_read_support_filter_bzip2(a);
    archive_read_support_filter_xz(a);archive_read_support_filter_lzma(a);archive_read_support_filter_zstd(a);
    const char *suffix=psmtp_archive_suffix(job->sources->name);
    int raw=suffix && (!strcasecmp(suffix,".gz") || !strcasecmp(suffix,".bz2") || !strcasecmp(suffix,".xz") || !strcasecmp(suffix,".zst") || !strcasecmp(suffix,".lzma"));
    char raw_name[256];
    if(raw){
        archive_read_support_format_raw(a);
        archive_read_support_format_empty(a);
        snprintf(raw_name,sizeof(raw_name),"%.*s",(int)(suffix-job->sources->name),job->sources->name);
    }else{
        archive_read_support_format_zip(a);archive_read_support_format_7zip(a);
        archive_read_support_format_rar(a);archive_read_support_format_rar5(a);
        archive_read_support_format_tar(a);archive_read_support_format_cpio(a);
    }
    archive_read_set_callback_data(a,&input);
    archive_read_set_read_callback(a,read_archive);
    archive_read_set_seek_callback(a,seek_archive);
    if((job->password[0] && archive_read_add_passphrase(a,job->password)!=ARCHIVE_OK) || archive_read_open1(a)!=ARCHIVE_OK){rc=archive_status(a,job->password);goto done;}
    if(raw && archive_filter_code(a,0)==ARCHIVE_FILTER_NONE){rc=MTP_PROTOCOL;goto done;}
    size_t count=0;
    for(;;){
        if(psmtp_work_progress(work,(enum mtp_copy_phase)PSMTP_OP_EXTRACT,consumed(a,total),total)){rc=MTP_CANCELLED;break;}
        struct archive_entry *entry=NULL;
        int status=archive_read_next_header(a,&entry);
        if(status==ARCHIVE_EOF){
            rc=0;
            if(raw && archive_format(a)==ARCHIVE_FORMAT_EMPTY){
                char path[ARCHIVE_PATH_LIMIT];
                rc=normalize_entry(raw_name,path);
                if(!rc && !path[0])rc=MTP_PROTOCOL;
                if(!rc){
                    int fd=open_entry(target,path,0);
                    if(fd<0 || close(fd))rc=fs_error();
                }
            }
            break;
        }
        if(status!=ARCHIVE_OK || !entry){rc=archive_status(a,job->password);break;}
        if(archive_entry_is_encrypted(entry)){
            if((archive_format(a)&ARCHIVE_FORMAT_BASE_MASK)!=ARCHIVE_FORMAT_ZIP){
                rc=PSMTP_ARCHIVE_ENCRYPTION;break;
            }
            if(!job->password[0]){
                rc=PSMTP_ARCHIVE_REQUIRED;break;
            }
        }
        if(++count>ARCHIVE_ENTRY_LIMIT){rc=MTP_LIMIT;break;}
        const char *name=raw?raw_name:archive_entry_pathname_utf8(entry);
        if(!name)name=archive_entry_pathname(entry);
        char path[ARCHIVE_PATH_LIMIT];
        mode_t type=archive_entry_filetype(entry);
        if((type!=AE_IFREG && type!=AE_IFDIR) || archive_entry_hardlink(entry) || archive_entry_symlink(entry)){rc=MTP_PROTOCOL;break;}
        rc=normalize_entry(name,path);
        if(rc)break;
        if(!path[0]){if(type==AE_IFDIR)continue;rc=MTP_PROTOCOL;break;}
        rc=remember(paths,path);
        if(rc)break;
        int fd=open_entry(target,path,type==AE_IFDIR);
        if(fd<0){rc=fs_error();break;}
        if(type==AE_IFREG){
            rc=extract_entry_data(a,entry,fd,job->password,total,work);
            if(!rc && fsync(fd))rc=fs_error();
        }
        if(close(fd) && !rc)rc=fs_error();
        if(rc)break;
    }
    if(!rc && archive_read_close(a)!=ARCHIVE_OK)rc=archive_status(a,job->password);
done:
    fprintf(psmtp_work_log(work),"stage=archive-extract status=%d native=%d format=%d passphrase-supplied=%d bytes=%llu phone-source=%d\n",
            rc,a?archive_errno(a):0,a?archive_format(a):0,job->password[0]!=0,(unsigned long long)total,job->client!=NULL);
    if(a)archive_read_free(a);
    if(paths){for(size_t i=0;i<2*ARCHIVE_ENTRY_LIMIT;++i)free(paths[i]);free(paths);}
    free(input.buffer);
    return rc;
}

static int extract_job(psmtp_archive_job *job,const char *target,psmtp_work *work){
    int input=-1,rc=0;
    if(job->client){
        if(!job->staged_path[0])rc=psmtp_stage_file(job->sources,job->client,job->staged_directory,job->staged_path,work);
        if(!rc){input=open(job->staged_path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);if(input<0)rc=fs_error();}
        fprintf(psmtp_work_log(work),"stage=archive-phone-source status=%d passphrase-supplied=%d\n",rc,job->password[0]!=0);
    }else rc=psmtp_op_open_read(job->sources,&input);
    if(!rc){psmtp_native_stat stat;rc=psmtp_native_status(input,&stat);if(!rc)rc=extract_contents(job,input,target,(uint64_t)stat.st_size,work);}
    if(input>=0)close(input);
    return rc;
}

static int archive_copy_entry(struct archive *a,int source,unsigned char *buffer,uint64_t left,
                              uint64_t *done,uint64_t total,enum mtp_copy_phase phase,
                              const char *password,psmtp_work *work){
    while(left){
        ssize_t n=read(source,buffer,left>ARCHIVE_IO_BYTES?ARCHIVE_IO_BYTES:(size_t)left);
        if(n<0 && errno==EINTR)continue;
        if(n<=0)return MTP_FILESYSTEM;
        if(archive_write_data(a,buffer,(size_t)n)!=n)return archive_status(a,password);
        left-=(uint64_t)n;
        *done+=(uint64_t)n;
        if(psmtp_work_progress(work,phase,*done,total))return MTP_CANCELLED;
    }
    return 0;
}

typedef struct {
    struct archive *archive;
    psmtp_work *work;
    uint64_t offset,size,*done,total;
} phone_archive_entry;

static int phone_archive_write(void *context,const uint8_t *data,size_t bytes){
    phone_archive_entry *entry=context;
    if(!bytes || bytes>entry->size-entry->offset)return MTP_VERIFY;
    if(archive_write_data(entry->archive,data,bytes)!=(la_ssize_t)bytes)
        return archive_status(entry->archive,"");
    entry->offset+=bytes;
    *entry->done+=bytes;
    return 0;
}

static int phone_archive_progress(void *context,uint64_t received,uint64_t size){
    phone_archive_entry *entry=context;
    (void)received;(void)size;
    return psmtp_work_progress(entry->work,(enum mtp_copy_phase)PSMTP_OP_COMPRESS,*entry->done,entry->total);
}

static int archive_copy_phone(struct archive *a,mtp_client *client,const mtp_copy_source *source,
                              unsigned char *buffer,uint64_t *done,uint64_t total,psmtp_work *work){
    phone_archive_entry entry={a,work,0,source->size,done,total};
    if(!source->size)return 0;
    if(source->streamed){
        mtp_download_stats stats={0};
        int rc=psmtp_stream_download(client,source->handle,source->size,buffer,ARCHIVE_IO_BYTES,
            phone_archive_write,phone_archive_progress,&entry,&stats,psmtp_work_log(work));
        if(!rc)return entry.offset==source->size?0:MTP_VERIFY;
        /* Match the normal MTP copy fallback. A partially delivered object
         * must never be replayed into an already-started archive member. */
        if(rc!=MTP_RESPONSE || client->response!=0x2005 || client->poisoned || entry.offset)return rc;
    }
    if(!source->partial_supported || (!source->extended && source->size>=UINT32_MAX))return MTP_UNSUPPORTED;
    while(entry.offset<source->size){
        if(phone_archive_progress(&entry,0,0))return MTP_CANCELLED;
        uint64_t left=source->size-entry.offset;
        size_t request=left>MTP_COPY_CHUNK?MTP_COPY_CHUNK:(size_t)left,got=0;
        int rc=mtp_get_partial(client,source->handle,entry.offset,request,source->extended,buffer,&got);
        if(rc)return rc;
        if(!got || got>request)return MTP_VERIFY;
        rc=phone_archive_write(&entry,buffer,got);
        if(rc)return rc;
    }
    return 0;
}

static int write_plan(struct archive *a,const psmtp_op_plan *plan,mtp_client *client,unsigned char *buffer,
                      uint64_t *done,uint64_t total,psmtp_work *work){
    char (*names)[ARCHIVE_PATH_LIMIT]=calloc(plan->count,sizeof(*names));
    struct archive_entry *entry=archive_entry_new();
    int rc=0;
    if(!names || !entry){rc=MTP_MEMORY;goto finish;}
    for(size_t i=0;i<plan->count;++i){
        const psmtp_op_node *node=&plan->nodes[i];
        int name_length=i?snprintf(names[i],sizeof(*names),"%s/%s",names[node->parent],node->name):snprintf(names[i],sizeof(*names),"%s",node->name);
        if(name_length<0 || (size_t)name_length>=sizeof(*names)){rc=MTP_LIMIT;break;}
        if(psmtp_work_progress(work,(enum mtp_copy_phase)PSMTP_OP_COMPRESS,*done,total)){rc=MTP_CANCELLED;break;}
        archive_entry_clear(entry);
        archive_entry_set_pathname(entry,names[i]);
        archive_entry_set_filetype(entry,node->folder?AE_IFDIR:AE_IFREG);
        archive_entry_set_perm(entry,node->folder?0755:0644);
        archive_entry_set_nlink(entry,1);
        if(client){
            rc=psmtp_op_verify_phone_node(client,node);
            if(rc)break;
        }else {
            archive_entry_set_ino64(entry,node->native.st_ino);
            archive_entry_set_mtime(entry,(time_t)node->native.modify_time.seconds,(long)node->native.modify_time.nanoseconds);
        }
        archive_entry_set_size(entry,node->folder?0:client?(int64_t)node->phone.size:node->native.st_size);
        if(archive_write_header(a,entry)!=ARCHIVE_OK){rc=archive_status(a,"");break;}
        if(!node->folder && client){
            rc=archive_copy_phone(a,client,&node->phone,buffer,done,total,work);
            if(!rc)rc=psmtp_op_verify_phone_node(client,node);
        }else if(!node->folder){
            psmtp_op_ref source={.location=plan->source.location,.native=node->native};
            strcpy(source.path,node->path);
            int fd;
            rc=psmtp_op_open_read(&source,&fd);
            if(rc)break;
            rc=archive_copy_entry(a,fd,buffer,(uint64_t)node->native.st_size,done,total,
                (enum mtp_copy_phase)PSMTP_OP_COMPRESS,"",work);
            close(fd);
            if(!rc){rc=psmtp_op_open_read(&source,&fd);if(!rc)close(fd);}
        }
        if(rc)break;
        if(archive_write_finish_entry(a)!=ARCHIVE_OK){rc=archive_status(a,"");break;}
    }
finish:
    free(names);archive_entry_free(entry);
    return rc;
}

static int zip_options(struct archive *a,const char *level,const char *password){
    if(archive_write_set_format_option(a,"zip","compression-level",level)!=ARCHIVE_OK ||
       archive_write_set_format_option(a,"zip","hdrcharset","UTF-8")!=ARCHIVE_OK)return MTP_UNSUPPORTED;
    if(*password && (archive_write_set_passphrase(a,password)!=ARCHIVE_OK ||
       archive_write_set_format_option(a,"zip","encryption","aes256")!=ARCHIVE_OK))return PSMTP_ARCHIVE_ENCRYPTION;
    return 0;
}

static int compress_job(psmtp_archive_job *job,int target,psmtp_work *work){
    struct archive *a=archive_write_new();
    uint64_t done=0,total=0;
    int rc=MTP_MEMORY;
    if(!a)return rc;
    if((unsigned)job->format>=PSMTP_ARCHIVE_FORMATS ||
       job->level<psmtp_archive_min_level(job->format) || job->level>psmtp_archive_max_level(job->format)){
        rc=MTP_STATE;goto finish;
    }
    if(psmtp_archive_single_file(job->format) && (job->count!=1 || job->sources[0].folder)){
        rc=MTP_UNSUPPORTED;goto finish;
    }
    char level[2]={(char)('0'+job->level),0};
    const char *password=job->format==PSMTP_ARCHIVE_ZIP?job->password:"";
    if(archive_formats[job->format].set_format(a)!=ARCHIVE_OK){rc=MTP_UNSUPPORTED;goto finish;}
    if(job->format==PSMTP_ARCHIVE_ZIP){
        rc=zip_options(a,level,password);
        if(rc)goto finish;
    }else if(job->format==PSMTP_ARCHIVE_7Z){
        if(archive_write_set_format_option(a,"7zip","compression",job->level?"lzma2":"copy")!=ARCHIVE_OK ||
           archive_write_set_format_option(a,"7zip","compression-level",level)!=ARCHIVE_OK){
            rc=MTP_UNSUPPORTED;goto finish;
        }
    }
    const char *filter=archive_formats[job->format].filter;
    if(filter && (archive_formats[job->format].add_filter(a)!=ARCHIVE_OK ||
       archive_write_set_filter_option(a,filter,"compression-level",level)!=ARCHIVE_OK)){
        rc=MTP_UNSUPPORTED;goto finish;
    }
    archive_write_set_bytes_per_block(a,ARCHIVE_IO_BYTES);
    archive_write_set_bytes_in_last_block(a,1);
    archive_output output={target,work};
    if(archive_write_open(a,&output,NULL,write_archive,NULL)!=ARCHIVE_OK){rc=archive_status(a,password);goto finish;}
    psmtp_op_plan *plan=calloc(1,sizeof(*plan));
    unsigned char *buffer=malloc(ARCHIVE_IO_BYTES);
    if(!plan || !buffer){free(plan);free(buffer);rc=MTP_MEMORY;goto finish;}
    FILE *log=psmtp_work_log(work);
    rc=0;
    for(size_t i=0;i<job->count;++i){
        const psmtp_op_ref *source=&job->sources[i];
        for(size_t other=0;other<i;++other)if(!strcmp(job->sources[other].name,source->name)){rc=MTP_EXISTS;break;}
        if(rc)break;
        rc=psmtp_op_plan_build(plan,source,job->client,psmtp_work_progress,work,log);
        if(!rc){
            total=job->count==1?plan->bytes:0;
            rc=write_plan(a,plan,job->client,buffer,&done,total,work);
        }
        if(rc)break;
    }
    free(buffer);free(plan);
    if(!rc && job->format==PSMTP_ARCHIVE_7Z &&
       psmtp_work_progress(work,(enum mtp_copy_phase)PSMTP_OP_ARCHIVE_FINALIZE,0,0))rc=MTP_CANCELLED;
    if(!rc && archive_write_close(a)!=ARCHIVE_OK)rc=archive_status(a,password);
finish:
    if(rc)archive_write_fail(a);
    archive_write_free(a);
    return rc;
}

static int encrypt_archive(psmtp_archive_job *job,int source,int destination,psmtp_work *work){
    off_t length=lseek(source,0,SEEK_END);
    if(length<=0 || lseek(source,0,SEEK_SET)<0)return MTP_FILESYSTEM;
    size_t name_length=strlen(job->name);
    if(name_length<=4 || strcmp(job->name+name_length-4,".zip"))return MTP_STATE;
    char member[256];
    snprintf(member,sizeof(member),"%.*s",(int)(name_length-4),job->name);
    if(!psmtp_name_valid(member))return MTP_STATE;

    enum mtp_copy_phase phase=(enum mtp_copy_phase)PSMTP_OP_ARCHIVE_ENCRYPT;
    if(psmtp_work_progress(work,phase,0,(uint64_t)length))return MTP_CANCELLED;
    struct archive *a=archive_write_new();
    struct archive_entry *entry=archive_entry_new();
    unsigned char *buffer=malloc(ARCHIVE_IO_BYTES);
    int rc=MTP_MEMORY;
    if(!a || !entry || !buffer)goto finish;
    rc=MTP_UNSUPPORTED;
    if(archive_write_set_format_zip(a)!=ARCHIVE_OK)goto finish;
    rc=zip_options(a,"0",job->password);
    if(rc)goto finish;
    archive_write_set_bytes_per_block(a,ARCHIVE_IO_BYTES);
    archive_write_set_bytes_in_last_block(a,1);
    archive_output output={destination,work};
    archive_entry_set_pathname(entry,member);
    archive_entry_set_filetype(entry,AE_IFREG);
    archive_entry_set_perm(entry,0644);
    archive_entry_set_size(entry,length);
    if(archive_write_open(a,&output,NULL,write_archive,NULL)!=ARCHIVE_OK ||
       archive_write_header(a,entry)!=ARCHIVE_OK){rc=archive_status(a,job->password);goto finish;}
    uint64_t done=0;
    rc=archive_copy_entry(a,source,buffer,(uint64_t)length,&done,(uint64_t)length,phase,job->password,work);
    if(!rc && archive_write_close(a)!=ARCHIVE_OK)rc=archive_status(a,job->password);
finish:
    if(a){
        if(rc)archive_write_fail(a);
        archive_write_free(a);
    }
    archive_entry_free(entry);
    free(buffer);
    return rc;
}

static int compress_output(psmtp_archive_job *job,int destination,psmtp_work *work){
    if(!job->password[0] || job->format==PSMTP_ARCHIVE_ZIP)return compress_job(job,destination,work);
    int source=psmtp_archive_scratch(NULL);
    if(source<0)return fs_error();
    int rc=compress_job(job,source,work);
    if(!rc)rc=encrypt_archive(job,source,destination,work);
    close(source);
    return rc;
}

static int archive_run_local(psmtp_archive_job *job,psmtp_work *work){
    int dir=psmtp_op_writable_directory(&job->destination,job->directory);
    if(dir<0)return fs_error();
    char stage[PSMTP_LOCAL_PATH],output[PSMTP_LOCAL_PATH];
    int n=snprintf(output,sizeof(output),"%s/%s",job->directory,job->name);
    if(n<0 || (size_t)n>=sizeof(output)){close(dir);return MTP_LIMIT;}
    int occupied=open(output,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
    if(occupied>=0 || errno!=ENOENT){
        int rc=occupied>=0 || errno==ELOOP?MTP_EXISTS:fs_error();
        if(occupied>=0)close(occupied);
        close(dir);return rc;
    }
    psmtp_native_stat original,current;
    if(psmtp_native_status(dir,&original)){close(dir);return MTP_FILESYSTEM;}
    int fd=temporary(job->directory,stage,job->extracting);
    if(fd<0){int rc=fs_error();close(dir);return rc;}
    /* Both MTP and local paths are UTF-8. libarchive consults LC_CTYPE
     * even for ZIP entries that explicitly carry the UTF-8 flag. */
    locale_t utf8=newlocale(LC_CTYPE_MASK,"C.UTF-8",(locale_t)0);
    locale_t previous=utf8?uselocale(utf8):(locale_t)0;
    int rc=previous?(job->extracting?extract_job(job,stage,work):compress_output(job,fd,work)):MTP_UNSUPPORTED;
    if(previous)uselocale(previous);
    if(utf8)freelocale(utf8);
    if(!rc && psmtp_work_cancelled(work))rc=MTP_CANCELLED;
    if(!rc)rc=psmtp_native_output_access(fd,job->extracting,job->destination.kind==PSMTP_LOCATION_USB,psmtp_work_log(work));
    if(!rc && fsync(fd))rc=fs_error();
    int check=psmtp_op_writable_directory(&job->destination,job->directory);
    int same=check>=0 && !psmtp_native_status(check,&current) && current.st_dev==original.st_dev && current.st_ino==original.st_ino;
    if(check>=0)close(check);
    if(!same && !rc)rc=MTP_VERIFY;
    if(!rc)rc=publish(stage,output,job->extracting);
    close(fd);
    if(rc && same){if(job->extracting){remove_tree(stage);rmdir(stage);}else unlink(stage);}
    if(!rc)(void)fsync(dir);
    close(dir);
    return rc;
}

int psmtp_archive_run(void *context,psmtp_work *work){
    psmtp_archive_job *job=context;
    if(!job->count || !psmtp_name_valid(job->name))return MTP_STATE;
    FILE *log=psmtp_work_log(work);
    if((job->client && (job->extracting || job->destination.kind==PSMTP_LOCATION_PHONE)) || (!job->extracting &&
       (job->format==PSMTP_ARCHIVE_7Z || (job->password[0] && job->format!=PSMTP_ARCHIVE_ZIP)))){
        int rc=psmtp_local_prepare_copies(log);
        if(rc)return rc;
    }
    if(job->destination.kind!=PSMTP_LOCATION_PHONE)return archive_run_local(job,work);
    if(!job->client)return MTP_UNSUPPORTED;

    psmtp_archive_job local=*job;
    local.destination=(psmtp_location){.kind=PSMTP_LOCATION_INTERNAL,.root="/data"};
    strcpy(local.directory,PSMTP_COPY_DIRECTORY "/.upload-XXXXXXXX");
    if(!mkdtemp(local.directory)){psmtp_archive_forget(local.password);return fs_error();}
    if(job->extracting)strcpy(local.name,"extracted");
    int rc=archive_run_local(&local,work);
    psmtp_archive_forget(local.password);
    if(job->extracting){
        strcpy(job->staged_directory,local.staged_directory);
        strcpy(job->staged_path,local.staged_path);
    }
    psmtp_local_entry entry={.folder=job->extracting};
    strcpy(entry.name,job->name);
    int n=snprintf(entry.path,sizeof(entry.path),"%s/%s",local.directory,local.name);
    if(n<0 || (size_t)n>=sizeof(entry.path))rc=MTP_LIMIT;
    if(!rc){
        psmtp_op_plan *plan=calloc(1,sizeof(*plan));
        if(!plan)rc=MTP_MEMORY;
        else {
            psmtp_op_ref source;
            rc=psmtp_op_capture(&source,&local.destination,&entry);
            if(!rc)rc=psmtp_op_plan_build(plan,&source,NULL,psmtp_work_progress,work,log);
            if(!rc)rc=psmtp_op_target_phone(plan,job->client,&job->destination,job->phone_storage,job->phone_parent);
            if(!rc && (!job->sources[0].identity_valid || memcmp(plan->destination_identity,job->sources[0].phone_identity,32)))rc=MTP_VERIFY;
            if(!rc)rc=psmtp_op_execute(plan,job->client,0,psmtp_work_progress,work);
            job->uncertain_upload=plan->uncertain_upload || plan->cleanup_failed ||
                (job->extracting && rc && plan->nodes[0].created);
            free(plan);
        }
    }
    if(n>=0 && (size_t)n<sizeof(entry.path)){
        if(job->extracting){remove_tree(entry.path);rmdir(entry.path);}
        else unlink(entry.path);
    }
    rmdir(local.directory);
    return rc;
}
