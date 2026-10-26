/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
/* One worker owns file/USB reads and decoding. Only the UI owns cache entries.
 * Release/acquire publication transfers an image without shared mutation. */
#include "preview.h"
#include "psmtp/ui_format.h"
#ifdef PSMTP_TEST_PREVIEW
#include "ps4_preview_test_api.h"
#else
#include <orbis/libkernel.h>
#endif
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dirent_check.h"
typedef struct {char key[400],path[256],root[256],name[256];uint64_t bytes;int folder,phone,thumb,partial,extended;mtp_client *client;mtp_object_info info;} request;
typedef struct {char key[400];psmtp_metadata data;uint64_t used;} entry;
static struct {int started,stop,state,cancel;FILE *log;int32_t native_error;OrbisPthread thread;request job;psmtp_metadata result;entry cache[24];uint64_t clock,revision,last_change,generation;char selected[400];const mtp_navigator *phone;const psmtp_local_browser *local;psmtp_location location;} preview;
static int state(void){return __atomic_load_n(&preview.state,__ATOMIC_ACQUIRE);}
int psmtp_preview_phone_busy(void){int s=state();return (s==1 || s==2) && preview.job.phone;}
void psmtp_preview_cancel(void){__atomic_store_n(&preview.cancel,1,__ATOMIC_RELEASE);}
static int make_request(size_t index,request *r){
    memset(r,0,sizeof(*r));r->phone=preview.phone!=NULL;
    if(r->phone){const mtp_navigator *nav=preview.phone;const mtp_nav_entry *e;if(index>=nav->count)return -1;if(nav->catalog)e=&nav->catalog[index];else {if(index<nav->page_start || index-nav->page_start>=nav->page_count)return -1;e=&nav->entries[index-nav->page_start];}
        r->client=nav->client;r->info=e->info;r->bytes=e->exact_size?e->bytes:e->info.size32;r->folder=mtp_is_folder(&e->info);r->thumb=nav->thumbnail_supported;r->partial=nav->partial_supported;r->extended=nav->partial64_supported;
        snprintf(r->path,sizeof(r->path),"%u",e->handle);snprintf(r->name,sizeof(r->name),"%.255s",e->info.name);snprintf(r->key,sizeof(r->key),"p:%llu:%u:%u:%llu:%.32s:%.255s",(unsigned long long)preview.location.connection,e->info.storage,e->handle,(unsigned long long)r->bytes,e->info.modified,e->info.name);
    }else {const psmtp_local_entry *e;if(!preview.local || index>=preview.local->count)return -1;e=&preview.local->entries[index];snprintf(r->path,sizeof(r->path),"%s",e->path);snprintf(r->root,sizeof(r->root),"%s",preview.local->root);snprintf(r->name,sizeof(r->name),"%.255s",e->name);r->bytes=e->bytes;r->folder=e->folder;snprintf(r->key,sizeof(r->key),"n:%s:%llu:%s",e->path,(unsigned long long)e->bytes,e->modified);}
    {char key[400];snprintf(key,sizeof(key),"%llu:%.375s",(unsigned long long)preview.generation,r->key);snprintf(r->key,sizeof(r->key),"%s",key);}
    return 0;
}
static entry *find(const char *key){unsigned i;for(i=0;i<24;++i)if(preview.cache[i].used && !strcmp(key,preview.cache[i].key))return &preview.cache[i];return NULL;}
const psmtp_metadata *psmtp_preview_lookup(void *unused,size_t index){request r;entry *e;(void)unused;if(make_request(index,&r))return NULL;e=find(r.key);if(e){e->used=++preview.clock;return &e->data;}return NULL;}
typedef struct {request *job;int fd;} reader_context;
static int read_source(void *context,uint64_t offset,uint8_t *bytes,size_t length){
    reader_context *r=context;size_t done=0;if(__atomic_load_n(&preview.stop,__ATOMIC_ACQUIRE) || __atomic_load_n(&preview.cancel,__ATOMIC_ACQUIRE))return MTP_CANCELLED;
    if(r->job->phone){uint32_t id=(uint32_t)strtoul(r->job->path,NULL,10);while(done<length){size_t got=0,ask=length-done;int rc;if(__atomic_load_n(&preview.cancel,__ATOMIC_ACQUIRE))return MTP_CANCELLED;if(ask>MTP_COPY_CHUNK)ask=MTP_COPY_CHUNK;rc=mtp_get_partial(r->job->client,id,offset+done,ask,r->job->extended,bytes+done,&got);if(rc)return rc;if(!got)return MTP_PROTOCOL;done+=got;}return 0;}
    if(offset>INT64_MAX || lseek(r->fd,(off_t)offset,SEEK_SET)!=(off_t)offset)return MTP_FILESYSTEM;
    while(done<length){ssize_t n=read(r->fd,bytes+done,length-done);if(n<0 && errno==EINTR)continue;if(n<=0 || (size_t)n>length-done)return MTP_FILESYSTEM;done+=(size_t)n;}return 0;
}
static void load(request *job,psmtp_metadata *out){
    reader_context ctx={job,-1};psmtp_meta_reader reader={&ctx,job->bytes,read_source,0};int rc=MTP_UNSUPPORTED;uint32_t id=(uint32_t)strtoul(job->path,NULL,10);psmtp_native_stat before;
    memset(out,0,sizeof(*out));out->loaded=1;if(__atomic_load_n(&preview.cancel,__ATOMIC_ACQUIRE) || __atomic_load_n(&preview.stop,__ATOMIC_ACQUIRE)){out->error=MTP_CANCELLED;return;}
    if(job->folder){
        if(job->phone){uint32_t *ids=malloc(MTP_MAX_ITEMS*4);size_t count=0;if(ids){rc=mtp_get_handles(job->client,job->info.storage,id,ids,MTP_MAX_ITEMS,&count);if(!rc){out->items=count;out->items_known=1;}free(ids);}}
        else {int pin=psmtp_native_directory(job->root,job->path);DIR *dir=pin>=0?opendir(job->path):NULL;struct dirent *e;if(dir){out->items_known=1;while(!__atomic_load_n(&preview.cancel,__ATOMIC_ACQUIRE) && (e=readdir(dir))){if(!psmtp_dirent_valid(e)){out->items_known=0;break;}if(strcmp(e->d_name,".") && strcmp(e->d_name,".."))++out->items;}closedir(dir);rc=0;}if(pin>=0)close(pin);}
        out->error=rc;return;
    }
    if(!job->phone){char parent[256],*slash;int pin;strcpy(parent,job->path);slash=strrchr(parent,'/');if(!slash)return;if(slash==parent)slash[1]=0;else *slash=0;pin=psmtp_native_directory(job->root,parent);if(pin<0)return;ctx.fd=open(job->path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);close(pin);if(ctx.fd<0)return;if(psmtp_native_status(ctx.fd,&before) || !S_ISREG(before.st_mode) || before.st_size<0 || (uint64_t)before.st_size!=job->bytes){close(ctx.fd);return;}}
    if(!job->phone || ((job->partial || job->extended) && (strcmp(psmtp_ui_kind(job->name,0),"Image") || job->bytes<=1024u*1024u))){rc=psmtp_metadata_load(&reader,job->name,out);}
    if(job->phone && !__atomic_load_n(&preview.cancel,__ATOMIC_ACQUIRE) && !out->image && job->thumb && !job->client->poisoned){uint8_t *data=NULL;size_t size=0;if(!mtp_get_thumbnail(job->client,id,&data,&size)){unsigned w=out->width,h=out->height;(void)psmtp_metadata_image(data,size,out);out->width=w;out->height=h;}free(data);}
    if(job->phone){if(job->info.image_width && job->info.image_height){out->width=job->info.image_width;out->height=job->info.image_height;}}
    else {psmtp_native_stat after;if(psmtp_native_status(ctx.fd,&after) || before.st_ino!=after.st_ino || before.st_size!=after.st_size || memcmp(&before.modify_time,&after.modify_time,sizeof(before.modify_time)))psmtp_metadata_free(out);close(ctx.fd);
        if(!out->image && !strcmp(psmtp_ui_kind(job->name,0),"Video")){char side[256],*dot;unsigned attempt;strcpy(side,job->path);dot=strrchr(side,'.');for(attempt=0;dot && attempt<2 && !out->image;++attempt){int fd;psmtp_native_stat st;uint8_t *data;size_t used=0;strcpy(dot,attempt?".png":".jpg");fd=open(side,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);if(fd<0)continue;if(psmtp_native_status(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<=0||st.st_size>4*1024*1024){close(fd);continue;}data=malloc((size_t)st.st_size);if(data){while(used<(size_t)st.st_size){ssize_t n=read(fd,data+used,(size_t)st.st_size-used);if(n<=0)break;used+=(size_t)n;}if(used==(size_t)st.st_size){unsigned w=out->width,h=out->height;(void)psmtp_metadata_image(data,used,out);out->width=w;out->height=h;}free(data);}close(fd);}}
    }out->loaded=1;out->error=rc;
}
static void *worker(void *unused){(void)unused;while(!__atomic_load_n(&preview.stop,__ATOMIC_ACQUIRE)){int expected=1;if(__atomic_compare_exchange_n(&preview.state,&expected,2,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){if(preview.job.phone){preview.job.client->background_io=1;preview.job.client->background_native_error=0;}
            load(&preview.job,&preview.result);
            if(preview.job.phone){preview.native_error=preview.job.client->background_native_error;if(preview.job.client->poisoned)preview.result.error=MTP_IO;preview.job.client->background_io=0;}
            else preview.native_error=0;
            __atomic_store_n(&preview.state,3,__ATOMIC_RELEASE);}else sceKernelUsleep(10000);}return NULL;}
void psmtp_preview_open(FILE *log){int rc;if(preview.started)return;preview.log=log;rc=scePthreadCreate(&preview.thread,NULL,worker,NULL,"psmtp-preview");if(!rc)preview.started=1;if(log)fprintf(log,"stage=preview-worker status=%d\n",rc);}
void psmtp_preview_invalidate(void){unsigned i;++preview.generation;for(i=0;i<24;++i){psmtp_metadata_free(&preview.cache[i].data);memset(&preview.cache[i],0,sizeof(entry));}preview.selected[0]=0;}
void psmtp_preview_close(FILE *log){psmtp_preview_cancel();if(preview.started){__atomic_store_n(&preview.stop,1,__ATOMIC_RELEASE);if(scePthreadJoin(preview.thread,NULL)){if(log)fputs("stage=preview-worker join-failed\n",log);return;}preview.started=0;}psmtp_metadata_free(&preview.result);psmtp_preview_invalidate();}
void psmtp_preview_frame(const psmtp_location *location,const mtp_navigator *phone,const psmtp_local_browser *local,int list,int allow,uint64_t now){
    size_t selected=phone?phone->selected:local?local->selected:0,count=phone?phone->count:local?local->count:0,limit=list?16:12,first=selected/limit*limit,k;request r;
    if(state()==3 && preview.native_error && preview.log){int32_t native=preview.native_error;int error=preview.result.error;preview.native_error=0;fprintf(preview.log,"stage=preview-read status=%d native-rc=0x%08x names-in-log=0\n",error,(unsigned)native);}
    if(state()==3 && __atomic_load_n(&preview.cancel,__ATOMIC_ACQUIRE)){psmtp_metadata_free(&preview.result);__atomic_store_n(&preview.state,0,__ATOMIC_RELEASE);}
    if(state()==3){unsigned i,slot=0;for(i=1;i<24;++i)if(preview.cache[i].used<preview.cache[slot].used)slot=i;psmtp_metadata_free(&preview.cache[slot].data);preview.cache[slot].data=preview.result;memset(&preview.result,0,sizeof(preview.result));preview.cache[slot].data.revision=++preview.revision;snprintf(preview.cache[slot].key,sizeof(preview.cache[slot].key),"%s",preview.job.key);preview.cache[slot].used=++preview.clock;__atomic_store_n(&preview.state,0,__ATOMIC_RELEASE);}
    preview.location=*location;preview.phone=phone;preview.local=local;
    if(!count || make_request(selected,&r))return;if(strcmp(preview.selected,r.key)){snprintf(preview.selected,sizeof(preview.selected),"%s",r.key);preview.last_change=now;}


    if(!allow || !preview.started || now-preview.last_change<200 || state() || (phone && !psmtp_preview_phone_busy() && phone->client->poisoned))return;
    for(k=0;k<=limit;++k){size_t index=k?first+k-1:selected;if(index>=count || make_request(index,&r) || find(r.key))continue;
        if(!r.folder && !strcmp(psmtp_ui_kind(r.name,0),"File"))continue;preview.job=r;__atomic_store_n(&preview.cancel,0,__ATOMIC_RELEASE);__atomic_store_n(&preview.state,1,__ATOMIC_RELEASE);break;}
}
