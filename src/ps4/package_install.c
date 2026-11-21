/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#define _GNU_SOURCE
#include "package_install.h"
#include "storage_space.h"
#include "copy_file.h"
#include "download.h"
#include "psmtp/version.h"
#ifdef PSMTP_TEST_INSTALL_NATIVE
#include "ps4_install_test_api.h"
#else
#include <orbis/libkernel.h>
#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>
#include <stdbool.h>
#include <orbis/AppInstUtil.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#endif
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <errno.h>
/* The SDK's BGFT header narrows unsigned long fields to 32 bits. The PS4
 * LP64 ABI below matches flatz's declarations and Itemzflow. Never use the
 * SDK's undersized progress structure for files above 4 GiB. */
typedef struct {int32_t user,entitlement;const char *id,*url,*extra,*name,*icon,*sku;uint32_t option;const char *scenario,*release,*type,*subtype;uint64_t size;} install_param;
typedef struct {install_param param;uint32_t slot;} install_storage_param;
typedef char install_storage_abi[(sizeof(install_storage_param)==112 && offsetof(install_storage_param,slot)==104)?1:-1];
typedef struct {uint32_t bits;int32_t error;uint64_t length,transferred,total,total_transferred;uint32_t index,count,rest,rest_total;int32_t preparing,copy;} install_progress;
typedef struct {void *heap;size_t size;} install_heap;
typedef char install_param_abi[(sizeof(install_param)==104 && offsetof(install_param,size)==96)?1:-1];
typedef char install_progress_abi[(sizeof(install_progress)==64 && offsetof(install_progress,transferred)==16 && offsetof(install_progress,copy)==60)?1:-1];
typedef char install_file_offsets[(sizeof(off_t)==8)?1:-1];
#include "psmtp/cleanup_log.h"

static struct {
    int ready,app_ready,app_loaded,bgft_loaded;void *heap;install_heap params;
    int32_t (*init)(install_heap *),(*term)(void),(*app_init)(void),(*app_term)(void);
    int32_t (*app_exists)(const char *,int32_t *);
    int32_t (*storage)(install_storage_param *,int32_t *);
    int32_t (*url)(install_param *,int32_t *);
    int32_t (*start)(int32_t),(*stop)(int32_t),(*unregister_task)(int32_t),(*progress)(int32_t,install_progress *);
} service;
struct psmtp_install_session {
    struct psmtp_install_session *next;
    psmtp_op_ref ref;mtp_client *client;mtp_copy_source phone;psmtp_pkg_source source;psmtp_pkg_info info;
    int fd,pin,stop,owned,task,complete,started,background,background_error,source_error,temporary,source_removed,copy_seen,last_percent,last_preparing,last_bytes_known,task_stopped,attempt_ended;
    psmtp_copy_file staging;
    char url[512];FILE *log;uint32_t native_error;int install_step,base_missing;
    int direct;psmtp_phone_server *bridge;psmtp_phone_control control;void *control_user;
    psmtp_phone_source stream;void (*release_stream)(void *);
    int direct_boost,direct_large_disabled;unsigned direct_fallbacks;size_t direct_chunk;uint64_t direct_read_calls,direct_read_bytes;
};
static psmtp_install_session *sessions;
static psmtp_install_failure pending_failure;static int failure_pending;
void psmtp_install_get_failure(const psmtp_install_session *s,int rc,psmtp_install_failure *out){
    memset(out,0,sizeof(*out));out->app_error=rc;if(!s)return;
    out->native_error=s->native_error;out->step=s->install_step;out->patch=s->info.patch;out->base_missing=s->base_missing;out->task=s->task;memcpy(out->title_id,s->info.title_id,sizeof(out->title_id));
}
int psmtp_install_take_failure(psmtp_install_failure *out){if(!failure_pending)return 0;*out=pending_failure;failure_pending=0;return 1;}
static void log_install_failure(psmtp_install_session *s,int rc){
    if(s->log)fprintf(s->log,"stage=install-failure phase=%s app-status=%d native=0x%08x reason=%s title-id=%.9s patch=%d task=%d base-missing=%d\n",psmtp_install_step_name(s->install_step),rc,s->native_error,psmtp_install_error_name(s->native_error),s->info.title_id,s->info.patch,s->task,s->base_missing);
}
static int installer_stopping;
void psmtp_install_stop(void){__atomic_store_n(&installer_stopping,1,__ATOMIC_RELEASE);}
static int stopped(psmtp_install_session *s){return __atomic_load_n(&installer_stopping,__ATOMIC_ACQUIRE) || __atomic_load_n(&s->stop,__ATOMIC_ACQUIRE);}
static int same_stat(const psmtp_native_stat *a,const psmtp_native_stat *b){return a->st_dev==b->st_dev && a->st_ino==b->st_ino && a->st_gen==b->st_gen && a->st_mode==b->st_mode && a->st_size==b->st_size && !memcmp(&a->modify_time,&b->modify_time,sizeof(a->modify_time)) && !memcmp(&a->change_time,&b->change_time,sizeof(a->change_time));}
static int source_check(psmtp_install_session *s){
    if(s->stream.read)return s->stream.validate(s->stream.user);
    if(s->client){mtp_object_info info;uint64_t size;int rc=mtp_get_object_info(s->client,s->ref.handle,&info);if(rc)return rc;
        if(info.storage!=s->phone.info.storage || info.parent!=s->phone.info.parent || info.format!=s->phone.info.format || info.size32!=s->phone.info.size32 || strcmp(info.name,s->phone.info.name) || strcmp(info.modified,s->phone.info.modified))return MTP_VERIFY;
        if(s->phone.size_property){rc=mtp_get_object_size(s->client,s->ref.handle,&size);if(rc)return rc;if(size!=s->source.size)return MTP_VERIFY;}return 0;
    }else {psmtp_native_stat st;return psmtp_native_status(s->fd,&st)?MTP_FILESYSTEM:!same_stat(&st,&s->ref.native)?MTP_VERIFY:0;}
}
static int source_read(void *v,uint64_t offset,uint8_t *bytes,size_t length){
    psmtp_install_session *s=v;size_t done=0;int rc=0;
    if(offset>s->source.size || length>s->source.size-offset)return MTP_LIMIT;
    if(s->client){while(done<length){int boosted=s->direct && !s->direct_large_disabled;size_t got=0,n=length-done,limit=s->direct?s->direct_chunk:MTP_COPY_CHUNK;if(stopped(s))return MTP_CANCELLED;if(n>limit)n=limit;
            if(boosted){rc=mtp_get_partial_buffered(s->client,s->ref.handle,offset+done,n,s->phone.extended,bytes+done,&got);++s->direct_read_calls;s->direct_read_bytes+=got;
                /* Only an explicit parameter rejection before a data phase
                 * permits choosing smaller future reads. Never replay after
                 * a timeout, malformed response, data phase or disconnect. */
                if(rc==MTP_UNSUPPORTED && !got && !s->client->poisoned && n>MTP_COPY_CHUNK){s->direct_chunk=n>MTP_PARTIAL_BASE_BYTES?MTP_PARTIAL_BASE_BYTES:MTP_COPY_CHUNK;s->direct_large_disabled=s->direct_chunk==MTP_COPY_CHUNK;++s->direct_fallbacks;continue;}}
            else rc=mtp_get_partial(s->client,s->ref.handle,offset+done,n,s->phone.extended,bytes+done,&got);
            if(rc || !got || got>n){if(!rc)rc=MTP_VERIFY;break;}done+=got;}}
    else {if(offset>INT64_MAX || lseek(s->fd,(off_t)offset,SEEK_SET)!=(off_t)offset)rc=MTP_FILESYSTEM;
        while(!rc && done<length){ssize_t got;if(stopped(s))return MTP_CANCELLED;do{got=read(s->fd,bytes+done,length-done);}while(got<0 && errno==EINTR);if(got<=0 || (size_t)got>length-done)rc=MTP_FILESYSTEM;else done+=(size_t)got;}}
    if(rc)__atomic_store_n(&s->source_error,rc,__ATOMIC_RELEASE);return rc;
}
static int find_module(const char *name){
    OrbisKernelModule ids[128];size_t count=0,i;int found=-1;memset(ids,255,sizeof(ids));if(sceKernelGetModuleList(ids,128,&count) || count>128)return -1;
    for(i=0;i<count;++i){OrbisKernelModuleInfo info;size_t n;const char *leaf;if(ids[i]>INT32_MAX)return -1;memset(&info,0,sizeof(info));info.size=sizeof(info);if(sceKernelGetModuleInfo(ids[i],&info))return -1;
        n=strnlen(info.name,sizeof(info.name));if(n==sizeof(info.name))return -1;leaf=strrchr(info.name,'/');leaf=leaf?leaf+1:info.name;
        n=strlen(name);if(!strncmp(leaf,name,n) && (!leaf[n] || !strcmp(leaf+n,".sprx") || !strcmp(leaf+n,".prx"))){if(found>=0)return -1;found=(int)ids[i];}}
    return found;
}
static int symbol(int module,const char *name,void *target,size_t n){void *address=NULL;if(module<0 || n!=sizeof(address) || sceKernelDlsym(module,name,&address) || !address)return -1;memcpy(target,&address,n);return 0;}
#define INSTALL_SYMBOL(field,name) symbol(module,name,&service.field,sizeof(service.field))
static int service_init(FILE *log,uint32_t *native_error,int *step){
    int module,app_module;int32_t rc;
    if(service.ready)return 0;
#ifndef PSMTP_TEST_INSTALL_NATIVE
    if(0)service.app_exists=&sceAppInstUtilAppExists; /* SDK prototype check; runtime resolution is optional. */
#endif
    *step=PSMTP_INSTALL_APP_INIT;
    rc=0;if(!service.app_loaded){rc=(int32_t)sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_APP_INST_UTIL);if(!rc)service.app_loaded=1;}fprintf(log,"stage=install-module appinst=0x%08x\n",(unsigned)rc);
    app_module=find_module("libSceAppInstUtil");if(symbol(app_module,"sceAppInstUtilInitialize",&service.app_init,sizeof(service.app_init)) || symbol(app_module,"sceAppInstUtilTerminate",&service.app_term,sizeof(service.app_term)))return MTP_UNSUPPORTED;
    service.app_exists=NULL;
    (void)symbol(app_module,"sceAppInstUtilAppExists",&service.app_exists,sizeof(service.app_exists));
    if(!service.app_ready){rc=service.app_init();fprintf(log,"stage=install-app-init rc=0x%08x\n",(unsigned)rc);if(rc){*native_error=(uint32_t)rc;return rc;}service.app_ready=1;}
    *step=PSMTP_INSTALL_BGFT_INIT;rc=0;if(!service.bgft_loaded){rc=(int32_t)sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_BGFT);if(!rc)service.bgft_loaded=1;}fprintf(log,"stage=install-module bgft=0x%08x\n",(unsigned)rc);module=find_module("libSceBgft");
    if(INSTALL_SYMBOL(init,"sceBgftServiceIntInit") || INSTALL_SYMBOL(term,"sceBgftServiceIntTerm") || INSTALL_SYMBOL(start,"sceBgftServiceDownloadStartTask") || INSTALL_SYMBOL(stop,"sceBgftServiceDownloadStopTask") || INSTALL_SYMBOL(unregister_task,"sceBgftServiceIntDownloadUnregisterTask") || INSTALL_SYMBOL(progress,"sceBgftServiceDownloadGetProgress"))return MTP_UNSUPPORTED;
    if(INSTALL_SYMBOL(storage,"sceBgftServiceIntDownloadRegisterTaskByStorageEx"))return MTP_UNSUPPORTED;
    service.heap=calloc(1,1024u*1024u);if(!service.heap)return MTP_MEMORY;service.params.heap=service.heap;service.params.size=1024u*1024u;rc=service.init(&service.params);fprintf(log,"stage=install-bgft-init api=sceBgftServiceIntInit rc=0x%08x\n",(unsigned)rc);
    if(rc){*native_error=(uint32_t)rc;free(service.heap);service.heap=NULL;return rc;}service.ready=1;return 0;
}
int psmtp_install_prepare(psmtp_install_session **out,const psmtp_op_ref *ref,mtp_client *client,psmtp_pkg_info *info,FILE *log){
    psmtp_install_session *s;int rc=0;char parent[256],*slash;*out=NULL;if(__atomic_load_n(&installer_stopping,__ATOMIC_ACQUIRE))return MTP_CANCELLED;
    {unsigned count=0;psmtp_install_session *at;for(at=sessions;at;at=at->next)++count;if(count>=16)return MTP_LIMIT;}
    if(!ref || ref->folder || !psmtp_pkg_name(ref->name))return MTP_UNSUPPORTED;
    s=calloc(1,sizeof(*s));if(!s)return MTP_MEMORY;s->next=sessions;sessions=s;s->fd=s->pin=s->task=-1;s->ref=*ref;s->log=log;s->source.user=s;s->source.read=source_read;
    if(ref->location.kind==PSMTP_LOCATION_PHONE){s->client=client;if(!client || !client->session || client->poisoned){rc=MTP_STATE;goto fail;}
        rc=mtp_copy_probe(client,ref->handle,&ref->info,&s->phone,log);if(rc)goto fail;
        s->source.size=s->phone.size;
    }else {if(strlen(ref->path)>=sizeof(parent)){rc=MTP_LIMIT;goto fail;}strcpy(parent,ref->path);slash=strrchr(parent,'/');if(!slash){rc=MTP_STATE;goto fail;}if(slash==parent)slash[1]=0;else *slash=0;
        s->pin=psmtp_native_directory(ref->location.root,parent);if(s->pin<0){rc=MTP_FILESYSTEM;goto fail;}s->fd=open(ref->path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);if(s->fd<0){rc=MTP_FILESYSTEM;goto fail;}
        if(!S_ISREG(ref->native.st_mode) || ref->native.st_size<0){rc=MTP_VERIFY;goto fail;}s->source.size=(uint64_t)ref->native.st_size;rc=source_check(s);if(rc)goto fail;
    }
    rc=psmtp_pkg_inspect(&s->source,&s->info);if(rc)goto fail;
    /* Inspect extracted a bare nine-character TITLE_ID, not a path or full
     * CONTENT_ID. Reject self-installation before confirmation, staging or BGFT. */
    if(!psmtp_pkg_install_title_allowed(s->info.title_id)){
        if(log)fprintf(log,"stage=install-identity-guard blocked=1 self=%d current-title=" PSMTP_TITLE_ID " status=%d staging=0 registered=0 source-removed=0\n",!strcmp(s->info.title_id,PSMTP_TITLE_ID),MTP_UNSUPPORTED);
        rc=MTP_UNSUPPORTED;goto fail;
    }
    if(log)fprintf(log,"stage=install-package title-id=%.9s type=0x%08x flags=0x%08x patch=%d bytes=%" PRIu64 " install-bytes=%" PRIu64 " source=%s\n",s->info.title_id,s->info.type,s->info.flags,s->info.patch,s->info.size,s->info.install_bytes,s->client?"phone":"native");
    *info=s->info;*out=s;return 0;
fail:
    (void)psmtp_install_close(s,0);return rc;
}
int psmtp_install_prepare_stream(psmtp_install_session **out,const char *name,const psmtp_phone_source *stream,
    void (*retain)(void *),void (*release)(void *),FILE *log){
    unsigned count=0;
    int rc;
    if(!out || !name || !stream || !stream->read || !stream->validate || !stream->available || !retain || !release)return MTP_STATE;
    *out=NULL;
    if(__atomic_load_n(&installer_stopping,__ATOMIC_ACQUIRE))return MTP_CANCELLED;
    for(psmtp_install_session *at=sessions;at;at=at->next)++count;
    if(count>=16 || strlen(name)>=sizeof(((psmtp_op_ref *)0)->name))return MTP_LIMIT;
    if(!psmtp_pkg_name(name))return MTP_UNSUPPORTED;
    psmtp_install_session *s=calloc(1,sizeof(*s));
    if(!s)return MTP_MEMORY;
    s->next=sessions;sessions=s;s->fd=s->pin=s->task=-1;s->log=log;s->direct=1;
    s->stream=*stream;s->release_stream=release;retain(stream->user);
    s->source=(psmtp_pkg_source){stream->user,stream->size,stream->read};
    strcpy(s->ref.name,name);
    s->install_step=PSMTP_INSTALL_INSPECT;
    rc=psmtp_pkg_inspect(&s->source,&s->info);
    if(!rc && !psmtp_pkg_install_title_allowed(s->info.title_id))rc=MTP_UNSUPPORTED;
    if(rc){log_install_failure(s,rc);(void)psmtp_install_close(s,0);return rc;}
    fprintf(log,"stage=usb-direct-install-inspect title-id=%.9s bytes=%" PRIu64 " install-bytes=%" PRIu64 "\n",
        s->info.title_id,s->info.size,s->info.install_bytes);
    *out=s;
    return MTP_OK;
}
int psmtp_install_stage(psmtp_install_session **session,int confirmed,mtp_copy_progress progress,void *user){
    psmtp_install_session *s=session?*session:NULL,*local_session=NULL;psmtp_op_ref local={0};psmtp_pkg_info check;psmtp_copy_file *file;
    mtp_copy_source source;mtp_copy_sink sink;mtp_copy_result result;psmtp_native_stat st;uint64_t required;int rc,fd;
    if(!s || !confirmed)return MTP_CANCELLED;if(!s->client || s->owned)return MTP_STATE;
    s->install_step=PSMTP_INSTALL_STAGE_COPY;
    if(s->source.size>UINT64_MAX-s->info.install_bytes)return MTP_LIMIT;
    required=s->source.size+s->info.install_bytes;
    rc=psmtp_space_require("/","/data",required,s->log);if(rc)return rc;
    rc=source_check(s);if(rc)return rc;
    source=s->phone;source.is_pkg=1;file=&s->staging;psmtp_copy_file_init(file,s->log,&sink);s->temporary=1;
    sink.rewind=sink.reopen_read; /* Start the full readback on a fresh read-only descriptor. */
    fprintf(s->log,"stage=install-copy begin bytes=%" PRIu64 " required=%" PRIu64 " network-init=0 source-delete=0 verify=full-crc64-reread\n",source.size,required);
    rc=mtp_copy_run_streamed(s->client,&source,&sink,progress,user,&result,s->log,MTP_COPY_VERIFY_CRC64,PSMTP_DOWNLOAD_READER);
    fprintf(s->log,"stage=install-copy end status=%d published=%d cleanup-warning=%d saved=%s\n",rc,result.published,result.cleanup_failed,file->path);
    if(rc)return rc;if(!result.published || result.cleanup_failed || !file->directory_readable)return MTP_VERIFY;
    fd=open(file->path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);if(fd<0)return MTP_FILESYSTEM;
    rc=psmtp_native_status(fd,&st);if(close(fd) && !rc)rc=MTP_FILESYSTEM;if(rc)return rc;
    if(!S_ISREG(st.st_mode) || st.st_dev!=file->file_dev || st.st_ino!=file->file_ino || st.st_size<0 || (uint64_t)st.st_size!=source.size)return MTP_VERIFY;
    local.location.kind=PSMTP_LOCATION_INTERNAL;strcpy(local.location.root,"/data");strcpy(local.location.label,"Internal storage");
    snprintf(local.path,sizeof(local.path),"%s",file->path);snprintf(local.name,sizeof(local.name),"%s",s->ref.name);local.native=st;
    rc=psmtp_install_prepare(&local_session,&local,NULL,&check,s->log);if(rc)return rc;
    if(memcmp(&check,&s->info,sizeof(check))){(void)psmtp_install_close(local_session,0);return MTP_VERIFY;}
    fprintf(s->log,"stage=install-copy validated=1 temporary=1 cleanup-on-attempt-end=1\n");
    local_session->temporary=1;local_session->staging=s->staging;s->temporary=0;(void)psmtp_install_close(s,0);*session=local_session;return 0;
}
int psmtp_install_phone_direct(psmtp_install_session *s,psmtp_phone_control control,void *user){
    if(!s || !s->client || s->owned || s->temporary || !control)return MTP_STATE;
    if(!s->phone.partial_supported || (s->source.size>=UINT32_MAX && !s->phone.extended))return MTP_UNSUPPORTED;
    s->direct=1;s->direct_boost=psmtp_stream_boost_get();s->direct_chunk=s->direct_boost && s->source.size>=8u*1024u*1024u?MTP_PARTIAL_BUFFER_BYTES:MTP_PARTIAL_BASE_BYTES;s->control=control;s->control_user=user;return 0;
}
static int direct_validate(void *user){return source_check(user);}
int psmtp_install_begin(psmtp_install_session *s,int confirmed){
    install_param params;psmtp_pkg_info check;int32_t rc,user=-1;int n;
    if(!s || !confirmed)return MTP_CANCELLED;if(stopped(s))return MTP_CANCELLED;if(s->owned || (s->client && !s->direct))return MTP_STATE;
    s->install_step=PSMTP_INSTALL_SOURCE_CHECK;
    rc=source_check(s);if(!rc)rc=psmtp_pkg_inspect(&s->source,&check);if(!rc && memcmp(&check,&s->info,sizeof(check)))rc=MTP_VERIFY;if(rc)return rc;
    uint64_t required=s->info.install_bytes;
    if(s->stream.available){
        uint64_t available=s->stream.available(s->stream.user);
        if(available>s->source.size || s->source.size-available>UINT64_MAX-required)return MTP_LIMIT;
        required+=s->source.size-available;
    }
    rc=psmtp_space_require("/","/data",required,s->log);if(rc)return rc;
    n=s->direct?0:snprintf(s->url,sizeof(s->url),"%s%s",!strncmp(s->ref.path,"/data/",6)?"/user":"",s->ref.path);
    if(n<0 || (size_t)n>=sizeof(s->url))return MTP_LIMIT;
    const char *transport=s->stream.read?"usb-growing-file":s->direct?"phone-loopback":"native-storage";
    fprintf(s->log,"stage=install-source transport=%s network-init=%d staging-copy=0\n",transport,s->direct);
    rc=service_init(s->log,&s->native_error,&s->install_step);if(rc){log_install_failure(s,rc);return rc;}if(stopped(s))return MTP_CANCELLED;
    if(s->info.patch){int32_t exists=-1,query=MTP_UNSUPPORTED;s->install_step=PSMTP_INSTALL_PATCH_BASE;
        if(service.app_exists)query=service.app_exists(s->info.title_id,&exists);
        if(s->log)fprintf(s->log,"stage=install-patch-base title-id=%.9s query=0x%08x exists=%d optional=1\n",s->info.title_id,(unsigned)query,exists);
        /* Only a successful, explicit absence blocks. Missing/error/unknown
         * queries keep the established storage registration path usable. */
        if(!query && !exists){s->base_missing=1;log_install_failure(s,MTP_UNSUPPORTED);return MTP_UNSUPPORTED;}
    }
    rc=sceUserServiceGetInitialUser(&user);if(rc){s->native_error=(uint32_t)rc;return rc;}
    memset(&params,0,sizeof(params));params.user=user;params.entitlement=5;params.id=s->info.content_id;params.url=s->url;params.name=s->ref.name;params.icon="";params.scenario="0";params.type=s->info.package_type;params.subtype="";params.size=s->source.size;params.option=0x10000;
    if(s->direct){int module=find_module("libSceBgft");psmtp_phone_source source=s->stream.read?s->stream:
            (psmtp_phone_source){s,s->client,s->source.size,source_read,direct_validate,s->control,s->control_user,s->direct_chunk,NULL};
        if(!s->stream.read)fprintf(s->log,"stage=install-phone-read speed-boost=%d mtp-request-max=%zu mode=buffered-range-pipeline\n",s->direct_boost,source.chunk_bytes);
        service.url=NULL;s->install_step=PSMTP_INSTALL_REGISTER;
        if(symbol(module,s->info.patch?"sceBgftServiceIntDebugDownloadRegisterPkg":"sceBgftServiceIntDownloadRegisterTask",&service.url,sizeof(service.url))){log_install_failure(s,MTP_UNSUPPORTED);return MTP_UNSUPPORTED;}
        rc=psmtp_phone_server_open(&s->bridge,&source,s->url,sizeof(s->url),s->log);if(rc){log_install_failure(s,rc);return rc;}
    }
    s->install_step=PSMTP_INSTALL_REGISTER;
    {int32_t task=-1;
        if(s->direct)rc=service.url(&params,&task);else {install_storage_param local={0};local.param=params;local.slot=0;rc=service.storage(&local,&task);}
        fprintf(s->log,"stage=install-register rc=0x%08x task=%d bytes=%" PRIu64 " phone=%d patch=%d transport=%s\n",(unsigned)rc,task,s->source.size,s->client!=NULL,s->info.patch,transport);
        /* A failed registration can return someone else's existing task ID. */
        if(rc){s->native_error=(uint32_t)rc;log_install_failure(s,rc);return rc;}if(task<0)return MTP_PROTOCOL;s->task=task;s->owned=1;}
    if(stopped(s))return MTP_CANCELLED;
    s->install_step=PSMTP_INSTALL_START;rc=service.start(s->task);fprintf(s->log,"stage=install-start rc=0x%08x task=%d\n",(unsigned)rc,s->task);if(rc)s->native_error=(uint32_t)rc;else s->started=1;if(rc)log_install_failure(s,rc);return rc;
}
int psmtp_install_poll(psmtp_install_session *s,psmtp_install_report *report){
    install_progress p;int32_t rc;memset(report,0,sizeof(*report));report->task=s->task;report->source_error=__atomic_load_n(&s->source_error,__ATOMIC_ACQUIRE);
    if(s->bridge){psmtp_phone_server_log(s->bridge,s->log);if(!report->source_error)report->source_error=psmtp_phone_server_error(s->bridge);report->requests=psmtp_phone_server_requests(s->bridge);}
    if(report->source_error){s->install_step=s->direct?PSMTP_INSTALL_PHONE_TRANSFER:PSMTP_INSTALL_SOURCE_CHECK;log_install_failure(s,report->source_error);return report->source_error;}
    if(!s->client && !s->background){rc=source_check(s);if(rc)return rc;}
    if(!s->owned)return MTP_STATE;
    s->install_step=PSMTP_INSTALL_PROGRESS_QUERY;memset(&p,0,sizeof(p));rc=service.progress(s->task,&p);report->native_error=rc?(uint32_t)rc:(uint32_t)p.error;
    if(report->native_error){s->native_error=report->native_error;if(!rc)s->install_step=PSMTP_INSTALL_PROGRESS_RESULT;
        psmtp_install_get_failure(s,MTP_IO,&report->failure);log_install_failure(s,MTP_IO);
        fprintf(s->log,"stage=install-system-error code=0x%08x transport=%s done=%" PRIu64 " total=%" PRIu64 "\n",report->native_error,s->direct?"phone-loopback":"native-storage",p.transferred,p.length);return MTP_IO;}
    /* Firmware can update aggregate bytes or local-copy percent while the
     * per-part transferred counter stays zero. Use real system values only. */
    report->done=p.total?p.total_transferred:p.transferred;report->total=p.total?p.total:p.length;
    report->copy_percent=p.copy;
    if(p.copy>0 && p.copy<=100)s->copy_seen=1;
    if(report->total){report->percent=(int)((long double)report->done*100/report->total);report->progress_known=1;}
    /* The native copy substage can already be 100 while aggregate bytes are
     * still advancing. Use it only when no byte total is available. */
    if(p.copy>0 && p.copy<=100 && !report->progress_known){report->percent=p.copy;report->progress_known=1;}
    report->preparing=!s->copy_seen && !report->done && p.preparing>0 && p.preparing<=100;
    if(report->preparing){report->percent=p.preparing;report->progress_known=1;}
    if(report->preparing!=s->last_preparing || (report->total!=0)!=s->last_bytes_known){s->last_percent=0;s->last_preparing=report->preparing;s->last_bytes_known=report->total!=0;}
    if(report->percent>100)report->percent=100;
    if(p.length && p.transferred>=p.length && (!p.total || p.total_transferred>=p.total) && (!s->copy_seen || p.copy==100) && (!s->background || p.copy==100) &&
        (!s->stream.available || s->stream.available(s->stream.user)==s->source.size)){
        s->complete=report->complete=1;report->percent=100;report->progress_known=1;
    }else if(report->percent>=100)report->percent=99;
    if(report->progress_known){if(report->percent<s->last_percent)report->percent=s->last_percent;else s->last_percent=report->percent;}
    fprintf(s->log,"stage=install-progress task=%d done=%" PRIu64 " total=%" PRIu64 " prepare=%d copy=%d percent=%d complete=%d\n",s->task,report->done,report->total,p.preparing,p.copy,report->percent,report->complete);
    return 0;
}
int psmtp_install_cleanup(psmtp_install_session *s){
    int rc;if(!s)return MTP_STATE;if(!s->temporary || s->source_removed)return 0;
    if(s->owned && !s->complete && !s->task_stopped)return MTP_STATE;
    rc=psmtp_copy_file_discard(&s->staging);
    if(!rc)s->source_removed=1;
    if(s->log)fprintf(s->log,"stage=install-cleanup status=%d owned-phone-copy=1 original-source-removed=0\n",rc);
    return rc;
}
uint32_t psmtp_install_native_error(const psmtp_install_session *s){return s?s->native_error:0;}
int psmtp_install_close_report(psmtp_install_session *s,int cancel,uint32_t *native_error){
    int rc=0,stopped_owned=0;psmtp_install_session **link=&sessions;if(!s)return 0;(void)cancel;
    stopped_owned=s->task_stopped;
    if(s->bridge)psmtp_phone_server_stop(s->bridge,!s->complete);
    if(s->owned && !s->complete && !s->background && !s->task_stopped){rc=service.stop(s->task);if(s->log)fprintf(s->log,"stage=install-stop rc=0x%08x task=%d owned=1\n",(unsigned)rc,s->task);if(!rc)s->task_stopped=stopped_owned=1;else if(!s->native_error)s->native_error=(uint32_t)rc;}
    if(s->bridge){int bridge_rc=psmtp_phone_server_close(&s->bridge,!s->complete,s->log);if(bridge_rc){if(s->log)fprintf(s->log,"stage=install-reader-retained status=%d safe-exit-required=1\n",bridge_rc);return bridge_rc;}}
    __atomic_store_n(&s->stop,1,__ATOMIC_RELEASE);
    if(stopped_owned){int cleanup=service.unregister_task(s->task);
        if(s->log)fprintf(s->log,"stage=install-unregister rc=0x%08x task=%d owned=1 source-removed=0\n",(unsigned)cleanup,s->task);
        if(!cleanup)s->owned=0;
        else {if(!rc)rc=cleanup;if(!s->native_error)s->native_error=(uint32_t)cleanup;}
        /* BGFT still owns a registered task when unregister fails. Keep its
         * staging source, then detach this process with service termination. */
    }
    s->attempt_ended=1;
    if(s->temporary && (!s->owned || s->complete)){int clean=psmtp_install_cleanup(s);if(clean && !rc)rc=clean;}
    else if(s->temporary && s->log)fputs("stage=install-staging retained=1 task-still-registered=1\n",s->log);
    if(s->fd>=0 && close(s->fd))rc=MTP_FILESYSTEM;if(s->pin>=0 && close(s->pin))rc=MTP_FILESYSTEM;
    if(s->log)fprintf(s->log,"stage=install-end task=%d delivered=%d staging-removed=%d original-source-removed=0 network-init=%d native=0x%08x\n",s->task,s->complete,s->source_removed,s->direct,s->native_error);
    if(s->client && s->direct && s->log)fprintf(s->log,"stage=install-phone-read-end buffered-calls=%" PRIu64 " buffered-bytes=%" PRIu64 " selected-request=%zu clean-size-rejection-fallbacks=%u\n",s->direct_read_calls,s->direct_read_bytes,s->direct_chunk,s->direct_fallbacks);
    if(native_error && !*native_error)*native_error=s->native_error;
    if(s->release_stream)s->release_stream(s->stream.user);
    while(*link && *link!=s)link=&(*link)->next;if(*link)*link=s->next;
    free(s);return rc;
}
int psmtp_install_close(psmtp_install_session *s,int cancel){return psmtp_install_close_report(s,cancel,NULL);}
int psmtp_install_close_session(psmtp_install_session **owner,int cancel,uint32_t *native){psmtp_install_session *at,*s=owner?*owner:NULL;int rc=psmtp_install_close_report(s,cancel,native);if(owner){for(at=sessions;at && at!=s;at=at->next){}if(!at)*owner=NULL;}return rc;}
int psmtp_install_handoff(psmtp_install_session **session,psmtp_install_report *report){
    psmtp_install_session *s=session?*session:NULL;int warning=0;
    if(!s || !report || !s->owned || !s->started || s->client || s->stream.read || s->background)return MTP_STATE;
    /* BGFT uses a native storage path, never an application callback. No USB,
     * borrowed stack, worker cookie or process-root descriptor is handed off. */
    s->background=1;
    if(s->fd>=0){if(close(s->fd))warning=MTP_FILESYSTEM;s->fd=-1;}
    if(s->pin>=0){if(close(s->pin))warning=MTP_FILESYSTEM;s->pin=-1;}
    memset(report,0,sizeof(*report));report->task=s->task;report->background=1;report->source_error=warning;
    if(s->log)fprintf(s->log,"stage=install-handoff task=%d native-storage=1 descriptors-closed=%d staging-retained=%d cleanup-warning=%d\n",s->task,!warning,s->temporary,warning);
    s->log=NULL;*session=NULL;return 0;
}
int psmtp_install_background_pending(void){psmtp_install_session *s;for(s=sessions;s;s=s->next)if(s->background)return 1;return 0;}
void psmtp_install_collect(FILE *log){
    psmtp_install_session *s=sessions;
    /* Serialized worker only; foreground jobs join maintenance before dispatch. A failed or
     * unknown progress query never authorizes deleting a system-owned source. */
    while(s){psmtp_install_session *next=s->next;
        if(s->background){psmtp_install_report report;int rc;s->log=log;
            rc=psmtp_install_poll(s,&report);
            if(!rc && report.complete)(void)psmtp_install_close(s,0);
            else if(rc!=s->background_error){s->background_error=rc;
                if(rc){psmtp_install_get_failure(s,rc,&pending_failure);failure_pending=1;}
                if(log)fprintf(log,"stage=install-background task=%d status=%d source-retained=1\n",s->task,rc);}
        }s=next;
    }
}
int psmtp_install_retained(void){return service.ready || service.app_ready || sessions!=NULL;}
int psmtp_install_phone_borrowed(void){psmtp_install_session *s;for(s=sessions;s;s=s->next)if(s->bridge)return 1;return 0;}
int psmtp_install_shutdown(FILE *log){
    int rc=0,first=0;unsigned count;
    /* prepare caps the session list; one pass must always unlink its head. */
    for(count=0;sessions && count<32;++count){psmtp_install_session *head=sessions;rc=psmtp_install_close(head,1);if(rc && !first)first=rc;if(sessions==head){if(!first)first=MTP_STATE;break;}}
    if(service.ready){rc=service.term();if(rc && !first)first=rc;fprintf(log,"stage=install-bgft-term api=sceBgftServiceIntTerm rc=0x%08x\n",(unsigned)rc);if(!rc){free(service.heap);service.heap=NULL;service.ready=0;}}
    if(service.app_ready && !service.ready){rc=service.app_term();if(rc && !first)first=rc;fprintf(log,"stage=install-app-term rc=0x%08x\n",(unsigned)rc);if(!rc)service.app_ready=0;}
    if(service.bgft_loaded && !service.ready){rc=sceSysmoduleUnloadModuleInternal(ORBIS_SYSMODULE_INTERNAL_BGFT);if(rc && !first)first=rc;fprintf(log,"stage=install-bgft-unload rc=0x%08x\n",(unsigned)rc);if(!rc)service.bgft_loaded=0;}
    if(service.app_loaded && !service.app_ready && !service.ready){rc=sceSysmoduleUnloadModuleInternal(ORBIS_SYSMODULE_INTERNAL_APP_INST_UTIL);if(rc && !first)first=rc;fprintf(log,"stage=install-app-unload rc=0x%08x\n",(unsigned)rc);if(!rc)service.app_loaded=0;}
    return first?first:psmtp_install_retained()?MTP_STATE:0;
}
