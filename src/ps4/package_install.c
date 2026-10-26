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
#ifdef PSMTP_TEST_INSTALL_NATIVE
#include "ps4_install_test_api.h"
#else
#include <orbis/libkernel.h>
#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>
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
static struct {
    int ready,app_ready;void *heap;install_heap params;
    int32_t (*init)(install_heap *),(*term)(void),(*app_init)(void);
    int32_t (*storage)(install_storage_param *,int32_t *);
    int32_t (*start)(int32_t),(*stop)(int32_t),(*unregister_task)(int32_t),(*progress)(int32_t,install_progress *);
} service;
struct psmtp_install_session {
    psmtp_op_ref ref;mtp_client *client;mtp_copy_source phone;psmtp_pkg_source source;psmtp_pkg_info info;
    int fd,pin,stop,owned,task,complete,source_error,temporary,source_removed,copy_seen,last_percent,last_preparing,task_stopped,attempt_ended;
    psmtp_copy_file staging;
    char url[512];FILE *log;uint32_t native_error;
};
static int stopped(psmtp_install_session *s){return __atomic_load_n(&s->stop,__ATOMIC_ACQUIRE);}
static int same_stat(const psmtp_native_stat *a,const psmtp_native_stat *b){return a->st_dev==b->st_dev && a->st_ino==b->st_ino && a->st_gen==b->st_gen && a->st_mode==b->st_mode && a->st_size==b->st_size && !memcmp(&a->modify_time,&b->modify_time,sizeof(a->modify_time)) && !memcmp(&a->change_time,&b->change_time,sizeof(a->change_time));}
static int source_check(psmtp_install_session *s){
    if(s->client){mtp_object_info info;uint64_t size;int rc=mtp_get_object_info(s->client,s->ref.handle,&info);if(rc)return rc;
        if(info.storage!=s->phone.info.storage || info.parent!=s->phone.info.parent || info.format!=s->phone.info.format || info.size32!=s->phone.info.size32 || strcmp(info.name,s->phone.info.name) || strcmp(info.modified,s->phone.info.modified))return MTP_VERIFY;
        if(s->phone.size_property){rc=mtp_get_object_size(s->client,s->ref.handle,&size);if(rc)return rc;if(size!=s->source.size)return MTP_VERIFY;}return 0;
    }else {psmtp_native_stat st;return psmtp_native_status(s->fd,&st)?MTP_FILESYSTEM:!same_stat(&st,&s->ref.native)?MTP_VERIFY:0;}
}
static int source_read(void *v,uint64_t offset,uint8_t *bytes,size_t length){
    psmtp_install_session *s=v;size_t done=0;int rc=0;
    if(offset>s->source.size || length>s->source.size-offset)return MTP_LIMIT;
    if(s->client){while(done<length){size_t got=0,n=length-done;if(stopped(s))return MTP_CANCELLED;if(n>MTP_COPY_CHUNK)n=MTP_COPY_CHUNK;
            rc=mtp_get_partial(s->client,s->ref.handle,offset+done,n,s->phone.extended,bytes+done,&got);if(rc || !got || got>n){if(!rc)rc=MTP_VERIFY;break;}done+=got;}}
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
static int service_init(FILE *log,uint32_t *native_error){
    int module,app_module;int32_t rc;
    if(service.ready)return 0;
    rc=(int32_t)sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_APP_INST_UTIL);fprintf(log,"stage=install-module appinst=0x%08x\n",(unsigned)rc);
    app_module=find_module("libSceAppInstUtil");if(symbol(app_module,"sceAppInstUtilInitialize",&service.app_init,sizeof(service.app_init)))return MTP_UNSUPPORTED;
    if(!service.app_ready){rc=service.app_init();fprintf(log,"stage=install-app-init rc=0x%08x\n",(unsigned)rc);if(rc){*native_error=(uint32_t)rc;return rc;}service.app_ready=1;}
    rc=(int32_t)sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_BGFT);fprintf(log,"stage=install-module bgft=0x%08x\n",(unsigned)rc);module=find_module("libSceBgft");
    if(INSTALL_SYMBOL(init,"sceBgftServiceIntInit") || INSTALL_SYMBOL(term,"sceBgftServiceIntTerm") || INSTALL_SYMBOL(start,"sceBgftServiceDownloadStartTask") || INSTALL_SYMBOL(stop,"sceBgftServiceDownloadStopTask") || INSTALL_SYMBOL(unregister_task,"sceBgftServiceIntDownloadUnregisterTask") || INSTALL_SYMBOL(progress,"sceBgftServiceDownloadGetProgress"))return MTP_UNSUPPORTED;
    if(INSTALL_SYMBOL(storage,"sceBgftServiceIntDownloadRegisterTaskByStorageEx"))return MTP_UNSUPPORTED;
    service.heap=calloc(1,1024u*1024u);if(!service.heap)return MTP_MEMORY;service.params.heap=service.heap;service.params.size=1024u*1024u;rc=service.init(&service.params);fprintf(log,"stage=install-bgft-init api=sceBgftServiceIntInit rc=0x%08x\n",(unsigned)rc);
    if(rc){*native_error=(uint32_t)rc;free(service.heap);service.heap=NULL;return rc;}service.ready=1;return 0;
}
int psmtp_install_prepare(psmtp_install_session **out,const psmtp_op_ref *ref,mtp_client *client,psmtp_pkg_info *info,FILE *log){
    psmtp_install_session *s;int rc=0;char parent[256],*slash;*out=NULL;
    if(!ref || ref->folder || !psmtp_pkg_name(ref->name))return MTP_UNSUPPORTED;
    s=calloc(1,sizeof(*s));if(!s)return MTP_MEMORY;s->fd=s->pin=s->task=-1;s->ref=*ref;s->log=log;s->source.user=s;s->source.read=source_read;
    if(ref->location.kind==PSMTP_LOCATION_PHONE){s->client=client;if(!client || !client->session || client->poisoned){rc=MTP_STATE;goto fail;}
        rc=mtp_copy_probe(client,ref->handle,&ref->info,&s->phone,log);if(rc)goto fail;
        s->source.size=s->phone.size;
    }else {if(strlen(ref->path)>=sizeof(parent)){rc=MTP_LIMIT;goto fail;}strcpy(parent,ref->path);slash=strrchr(parent,'/');if(!slash){rc=MTP_STATE;goto fail;}if(slash==parent)slash[1]=0;else *slash=0;
        s->pin=psmtp_native_directory(ref->location.root,parent);if(s->pin<0){rc=MTP_FILESYSTEM;goto fail;}s->fd=open(ref->path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);if(s->fd<0){rc=MTP_FILESYSTEM;goto fail;}
        if(!S_ISREG(ref->native.st_mode) || ref->native.st_size<0){rc=MTP_VERIFY;goto fail;}s->source.size=(uint64_t)ref->native.st_size;rc=source_check(s);if(rc)goto fail;
    }
    rc=psmtp_pkg_inspect(&s->source,&s->info);if(rc)goto fail;
    if(!strcmp(s->info.title_id,"PSMT00001")){rc=MTP_UNSUPPORTED;goto fail;}
    *info=s->info;*out=s;return 0;
fail:
    (void)psmtp_install_close(s,0);return rc;
}
int psmtp_install_stage(psmtp_install_session **session,int confirmed,mtp_copy_progress progress,void *user){
    psmtp_install_session *s=session?*session:NULL,*local_session=NULL;psmtp_op_ref local={0};psmtp_pkg_info check;psmtp_copy_file *file;
    mtp_copy_source source;mtp_copy_sink sink;mtp_copy_result result;psmtp_native_stat st;uint64_t required;int rc,fd;
    if(!s || !confirmed)return MTP_CANCELLED;if(!s->client || s->owned)return MTP_STATE;
    if(s->source.size>UINT64_MAX-s->info.install_bytes)return MTP_LIMIT;
    required=s->source.size+s->info.install_bytes;
    rc=psmtp_space_require("/","/data",required,s->log);if(rc)return rc;
    rc=source_check(s);if(rc)return rc;
    source=s->phone;source.is_pkg=1;file=&s->staging;psmtp_copy_file_init(file,s->log,&sink);s->temporary=1;
    fprintf(s->log,"stage=install-copy begin bytes=%" PRIu64 " required=%" PRIu64 " network-init=0 source-delete=0\n",source.size,required);
    rc=mtp_copy_run_quick(s->client,&source,&sink,progress,user,&result,s->log);
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
int psmtp_install_begin(psmtp_install_session *s,int confirmed){
    install_param params;psmtp_pkg_info check;int32_t rc,user=-1;int n;
    if(!s || !confirmed)return MTP_CANCELLED;if(s->owned || s->client)return MTP_STATE;
    rc=source_check(s);if(!rc)rc=psmtp_pkg_inspect(&s->source,&check);if(!rc && memcmp(&check,&s->info,sizeof(check)))rc=MTP_VERIFY;if(rc)return rc;
    rc=psmtp_space_require("/","/data",s->info.install_bytes,s->log);if(rc)return rc;
    n=snprintf(s->url,sizeof(s->url),"%s%s",!strncmp(s->ref.path,"/data/",6)?"/user":"",s->ref.path);
    if(n<0 || (size_t)n>=sizeof(s->url))return MTP_LIMIT;
    fprintf(s->log,"stage=install-source transport=native-storage network-init=0 staging-copy=0\n");
    rc=service_init(s->log,&s->native_error);if(rc)return rc;
    rc=sceUserServiceGetInitialUser(&user);if(rc){s->native_error=(uint32_t)rc;return rc;}
    memset(&params,0,sizeof(params));params.user=user;params.entitlement=5;params.id=s->info.content_id;params.url=s->url;params.name=s->ref.name;params.icon="";params.scenario="0";params.type=s->info.package_type;params.subtype="";params.size=s->source.size;params.option=0x10000;
    {int32_t task=-1;
        {install_storage_param local={0};local.param=params;local.slot=0;rc=service.storage(&local,&task);}
        fprintf(s->log,"stage=install-register rc=0x%08x task=%d bytes=%" PRIu64 " phone=%d patch=%d transport=%s\n",(unsigned)rc,task,s->source.size,s->client!=NULL,s->info.patch,"native-storage");
        /* A failed registration can return someone else's existing task ID. */
        if(rc){s->native_error=(uint32_t)rc;return rc;}if(task<0)return MTP_PROTOCOL;s->task=task;s->owned=1;}
    rc=service.start(s->task);fprintf(s->log,"stage=install-start rc=0x%08x task=%d\n",(unsigned)rc,s->task);if(rc)s->native_error=(uint32_t)rc;return rc;
}
int psmtp_install_poll(psmtp_install_session *s,psmtp_install_report *report){
    install_progress p;int32_t rc;memset(report,0,sizeof(*report));report->task=s->task;report->source_error=__atomic_load_n(&s->source_error,__ATOMIC_ACQUIRE);
    if(report->source_error)return report->source_error;
    if(!s->client){rc=source_check(s);if(rc)return rc;}
    if(!s->owned)return MTP_STATE;
    memset(&p,0,sizeof(p));rc=service.progress(s->task,&p);report->native_error=rc?(uint32_t)rc:(uint32_t)p.error;
    if(report->native_error){s->native_error=report->native_error;
        fprintf(s->log,"stage=install-system-error code=0x%08x transport=native-storage done=%" PRIu64 " total=%" PRIu64 "\n",report->native_error,p.transferred,p.length);return MTP_IO;}


    report->done=p.total?p.total_transferred:p.transferred;report->total=p.total?p.total:p.length;
    if(p.copy>0 && p.copy<=100)s->copy_seen=1;
    if(report->total){report->percent=(int)((long double)report->done*100/report->total);report->progress_known=1;}
    if(p.copy>0 && p.copy<=100 && (!report->progress_known || report->percent<p.copy)){report->percent=p.copy;report->progress_known=1;}
    report->preparing=!s->copy_seen && !report->done && p.preparing>0 && p.preparing<=100;
    if(report->preparing){report->percent=p.preparing;report->progress_known=1;}
    if(report->preparing!=s->last_preparing){s->last_percent=0;s->last_preparing=report->preparing;}
    if(report->percent>100)report->percent=100;
    if(p.length && p.transferred>=p.length && (!p.total || p.total_transferred>=p.total) && (!s->copy_seen || p.copy==100)){
        s->complete=report->complete=1;report->percent=100;report->progress_known=1;
    }else if(report->percent>=100)report->percent=99;
    if(report->progress_known){if(report->percent<s->last_percent)report->percent=s->last_percent;else s->last_percent=report->percent;}
    fprintf(s->log,"stage=install-progress task=%d done=%" PRIu64 " total=%" PRIu64 " prepare=%d copy=%d percent=%d complete=%d\n",s->task,report->done,report->total,p.preparing,p.copy,report->percent,report->complete);
    return 0;
}
int psmtp_install_cleanup(psmtp_install_session *s){
    int rc;if(!s)return MTP_STATE;if(!s->temporary || s->source_removed)return 0;
    if(s->owned && !s->complete && !s->task_stopped && !s->attempt_ended)return MTP_STATE;
    rc=psmtp_copy_file_discard(&s->staging);
    if(!rc)s->source_removed=1;
    if(s->log)fprintf(s->log,"stage=install-cleanup status=%d owned-phone-copy=1 original-source-removed=0\n",rc);
    return rc;
}
uint32_t psmtp_install_native_error(const psmtp_install_session *s){return s?s->native_error:0;}
int psmtp_install_close_report(psmtp_install_session *s,int cancel,uint32_t *native_error){
    int rc=0,stopped_owned=0;if(!s)return 0;
    if(cancel && s->owned && !s->complete){rc=service.stop(s->task);if(s->log)fprintf(s->log,"stage=install-stop rc=0x%08x task=%d owned=1\n",(unsigned)rc,s->task);if(!rc)s->task_stopped=stopped_owned=1;else if(!s->native_error)s->native_error=(uint32_t)rc;}
    __atomic_store_n(&s->stop,1,__ATOMIC_RELEASE);
    if(stopped_owned){int cleanup=service.unregister_task(s->task);
        if(s->log)fprintf(s->log,"stage=install-unregister rc=0x%08x task=%d owned=1 source-removed=0\n",(unsigned)cleanup,s->task);


    }
    s->attempt_ended=1;
    if(s->temporary){int clean=psmtp_install_cleanup(s);if(clean && !rc)rc=clean;}
    if(s->fd>=0 && close(s->fd))rc=MTP_FILESYSTEM;if(s->pin>=0 && close(s->pin))rc=MTP_FILESYSTEM;
    if(s->log)fprintf(s->log,"stage=install-end task=%d delivered=%d staging-removed=%d original-source-removed=0 network-init=0 native=0x%08x\n",s->task,s->complete,s->source_removed,s->native_error);
    if(native_error && !*native_error)*native_error=s->native_error;
    free(s);return rc;
}
int psmtp_install_close(psmtp_install_session *s,int cancel){return psmtp_install_close_report(s,cancel,NULL);}
void psmtp_install_shutdown(FILE *log){if(service.ready){int rc=service.term();fprintf(log,"stage=install-bgft-term api=sceBgftServiceIntTerm rc=0x%08x\n",(unsigned)rc);if(!rc){free(service.heap);service.heap=NULL;service.ready=0;}}}
