/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "update_client.h"
#include "update_config.h"
#include "psmtp/pkg.h"
#include "psmtp/sha256.h"
#include "file_store.h"
#include "storage_space.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <inttypes.h>
#ifndef PSMTP_TEST_UPDATER
#include "optional_module.h"
#include "monotonic.h"
#endif
#include "psmtp/cleanup_log.h"
#ifndef PSMTP_UPDATE_DIRECTORY
#define PSMTP_UPDATE_DIRECTORY "/data/pkg"
#endif
typedef struct {
    int32_t (*net_init)(void);void (*net_term)(void);
    int32_t (*pool_create)(const char *,int32_t,int32_t);void (*pool_destroy)(int32_t);
    int32_t (*ssl_init)(size_t),(*ssl_term)(int32_t),(*http_init)(int32_t,int32_t,size_t),(*http_term)(int32_t);
    int32_t (*template_create)(int32_t,const char *,int32_t,int32_t),(*template_delete)(int32_t);
    int32_t (*connect_create)(int32_t,const char *,int),(*connect_delete)(int32_t);
    int32_t (*request_create)(int32_t,int32_t,const char *,uint64_t),(*request_delete)(int32_t);
    int32_t (*send)(int32_t,const void *,size_t),(*status)(int32_t,int32_t *),(*read)(int32_t,void *,uint32_t);
    int32_t (*resolve_timeout)(int32_t,uint32_t),(*connect_timeout)(int32_t,uint32_t),(*send_timeout)(int32_t,uint32_t),(*recv_timeout)(int32_t,uint32_t);
    int32_t (*redirect)(int32_t,int),(*length)(int32_t,int *,uint64_t *);
} update_api;
typedef struct {
    update_api api;int modules[3],owned[3],net_owned,pool,ssl,http,template_id,connection,request;
    psmtp_work *work;FILE *log;uint64_t deadline;char url[2048];uint8_t buffer[65536];
} update_http;
static update_http *retained_http;
#ifdef PSMTP_TEST_UPDATER
static int update_platform_open(update_http *);
static int update_platform_unload(update_http *);
static uint64_t update_now(void);
#else
static uint64_t update_now(void){struct timespec t={0};if(psmtp_monotonic_time(&t))return 0;return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000;}
static int update_platform_open(update_http *h){
    static const char *const names[]={"libSceNet","libSceSsl","libSceHttp"};
    static const enum OrbisSysModuleInternal ids[]={ORBIS_SYSMODULE_INTERNAL_NET,ORBIS_SYSMODULE_INTERNAL_SSL,ORBIS_SYSMODULE_INTERNAL_HTTP};
    for(unsigned i=0;i<3;++i){h->modules[i]=psmtp_optional_module(names[i]);if(h->modules[i]<0){if(sceSysmoduleLoadModuleInternal(ids[i]))return MTP_UNSUPPORTED;h->owned[i]=1;h->modules[i]=psmtp_optional_module(names[i]);if(h->modules[i]<0)return MTP_UNSUPPORTED;}}
#define UP(i,field,name) do{if(psmtp_optional_symbol(h->modules[i],name,&h->api.field,sizeof(h->api.field)))return MTP_UNSUPPORTED;}while(0)
    UP(0,net_init,"sceNetInit");UP(0,net_term,"sceNetTerm");UP(0,pool_create,"sceNetPoolCreate");UP(0,pool_destroy,"sceNetPoolDestroy");
    UP(1,ssl_init,"sceSslInit");UP(1,ssl_term,"sceSslTerm");UP(2,http_init,"sceHttpInit");UP(2,http_term,"sceHttpTerm");
    UP(2,template_create,"sceHttpCreateTemplate");UP(2,template_delete,"sceHttpDeleteTemplate");UP(2,connect_create,"sceHttpCreateConnectionWithURL");UP(2,connect_delete,"sceHttpDeleteConnection");
    UP(2,request_create,"sceHttpCreateRequestWithURL");UP(2,request_delete,"sceHttpDeleteRequest");UP(2,send,"sceHttpSendRequest");UP(2,status,"sceHttpGetStatusCode");UP(2,read,"sceHttpReadData");
    UP(2,resolve_timeout,"sceHttpSetResolveTimeOut");UP(2,connect_timeout,"sceHttpSetConnectTimeOut");UP(2,send_timeout,"sceHttpSetSendTimeOut");UP(2,recv_timeout,"sceHttpSetRecvTimeOut");
    UP(2,redirect,"sceHttpSetAutoRedirect");UP(2,length,"sceHttpGetResponseContentLength");
#undef UP
    return 0;
}
static int update_platform_unload(update_http *h){
    static const enum OrbisSysModuleInternal ids[]={ORBIS_SYSMODULE_INTERNAL_NET,ORBIS_SYSMODULE_INTERNAL_SSL,ORBIS_SYSMODULE_INTERNAL_HTTP};
    for(int i=2;i>=0;--i)if(h->owned[i]){if(sceSysmoduleUnloadModuleInternal(ids[i]))return MTP_IO;h->owned[i]=0;}return 0;
}
#endif
static int update_check(update_http *h){uint64_t now;if(psmtp_work_cancelled(h->work))return MTP_CANCELLED;now=update_now();return !now || now>=h->deadline?MTP_TIMEOUT:0;}
static int http_close(update_http *h){int rc=0;
#define DROP(field,fn) do{if(h->field>=0){if(h->api.fn(h->field)){rc=MTP_IO;goto done;}h->field=-1;}}while(0)
    DROP(request,request_delete);DROP(connection,connect_delete);DROP(template_id,template_delete);DROP(http,http_term);DROP(ssl,ssl_term);
#undef DROP
    if(h->pool>=0){h->api.pool_destroy(h->pool);h->pool=-1;}if(h->net_owned){h->api.net_term();h->net_owned=0;}
    rc=update_platform_unload(h);
done:
    if(h->log)fprintf(h->log,"stage=update-http-close status=%d retained=%d\n",rc,rc!=0);
    if(rc)retained_http=h;else free(h);return rc;
}
static int http_open(update_http **out,const char *url,psmtp_work *work,int download){
    update_http *h;int rc;uint64_t now=update_now();*out=NULL;
    if(retained_http || !now || !psmtp_update_https(url))return MTP_UNSUPPORTED;
    h=calloc(1,sizeof(*h));if(!h)return MTP_MEMORY;*out=h;h->work=work;h->log=psmtp_work_log(work);h->pool=h->ssl=h->http=h->template_id=h->connection=h->request=-1;
    h->deadline=now+(download?UINT64_C(1800000):UINT64_C(8000));memcpy(h->url,url,strlen(url)+1);
    rc=update_platform_open(h);if(rc)return rc;
    /* No shared pool or TLS context; terminate only what this attempt owns.
     * An already initialized network stack is left alone by skipping online
     * checking if initialization cannot be acquired. */
    rc=h->api.net_init();if(rc)return MTP_IO;h->net_owned=1;
    h->pool=h->api.pool_create("psmtp-update",2*1024*1024,0);if(h->pool<0)return MTP_MEMORY;
    h->ssl=h->api.ssl_init(1024*1024);if(h->ssl<0)return MTP_IO;
    h->http=h->api.http_init(h->pool,h->ssl,2*1024*1024);if(h->http<0)return MTP_IO;
    h->template_id=h->api.template_create(h->http,"PS-MTP-Explorer/" PSMTP_VERSION,2,0);if(h->template_id<0)return MTP_IO;
    /* Keep the native TLS certificate/hostname/date checks enabled. No SSL
     * callback bypass, redirects, downgrade, credentials, or telemetry. */
    if(h->api.redirect(h->template_id,0) || h->api.resolve_timeout(h->template_id,2000000) ||
       h->api.connect_timeout(h->template_id,2000000) || h->api.send_timeout(h->template_id,2000000) || h->api.recv_timeout(h->template_id,download?5000000:2000000))return MTP_IO;
    rc=update_check(h);if(rc)return rc;
    h->connection=h->api.connect_create(h->template_id,h->url,0);if(h->connection<0)return MTP_IO;
    h->request=h->api.request_create(h->connection,0,h->url,0);if(h->request<0)return MTP_IO;
    rc=h->api.send(h->request,NULL,0);if(h->log)fprintf(h->log,"stage=update-http-send download=%d native=0x%08x\n",download,(unsigned)rc);if(rc)return MTP_IO;
    {int32_t status=0;if(h->api.status(h->request,&status))return MTP_IO;if(h->log)fprintf(h->log,"stage=update-http-status download=%d code=%d tls-verification=native-default\n",download,status);if(status!=200)return MTP_RESPONSE;}
    return update_check(h);
}
static int read_piece(update_http *h,void *bytes,size_t cap,size_t *got){int n,rc=update_check(h);*got=0;if(rc)return rc;n=h->api.read(h->request,bytes,(uint32_t)cap);if(n<0)return MTP_IO;if((unsigned)n>cap)return MTP_PROTOCOL;*got=(size_t)n;return update_check(h);}
static int header_read(void *v,uint64_t offset,uint8_t *out,size_t n){int fd=*(int *)v;if(offset>INT64_MAX || lseek(fd,(off_t)offset,SEEK_SET)!=(off_t)offset)return MTP_IO;
    while(n){ssize_t got=read(fd,out,n);if(got<0 && errno==EINTR)continue;if(got<=0 || (size_t)got>n)return MTP_IO;out+=got;n-=(size_t)got;}return 0;
}
static int save_package(update_http *h,psmtp_update_job *job){
    const psmtp_update_manifest *m=&job->manifest;char temporary[256],final[256],hex[65];int fd=-1,root=-1,rc,published=0;uint64_t done=0;size_t n;uint8_t digest[32];psmtp_sha256 hash;
    psmtp_pkg_source source;psmtp_pkg_info pkg;int length_type=-1;uint64_t content_length=0;
    job->saved_path[0]=0;
    if(h->api.length(h->request,&length_type,&content_length) || (length_type==0 && content_length!=m->size))return MTP_VERIFY;
    rc=psmtp_space_require("/data","/data",m->size+1048576,h->log);if(rc)return rc;
    if(mkdir(PSMTP_UPDATE_DIRECTORY,0777) && errno!=EEXIST)return MTP_FILESYSTEM;
    root=psmtp_native_directory("/data",PSMTP_UPDATE_DIRECTORY);if(root<0)return MTP_FILESYSTEM;
    for(unsigned i=0;i<32;++i)snprintf(hex+i*2,3,"%02x",m->sha256[i]);
    snprintf(final,sizeof(final),PSMTP_UPDATE_DIRECTORY "/PS-MTP-Explorer-%s-%.12s.pkg",m->version,hex);
    snprintf(temporary,sizeof(temporary),PSMTP_UPDATE_DIRECTORY "/.psmtp-update-XXXXXX");
    fd=mkstemp(temporary);if(fd<0){rc=MTP_FILESYSTEM;goto end;}
    psmtp_sha256_init(&hash);
    while(done<m->size){size_t want=(size_t)(m->size-done>sizeof(h->buffer)?sizeof(h->buffer):m->size-done),at=0;
        rc=read_piece(h,h->buffer,want,&n);if(rc)goto end;if(!n){rc=MTP_VERIFY;goto end;}
        while(at<n){ssize_t wrote=write(fd,h->buffer+at,n-at);if(wrote<0 && errno==EINTR)continue;if(wrote<=0 || (size_t)wrote>n-at){rc=errno==ENOSPC?MTP_NO_SPACE:MTP_FILESYSTEM;goto end;}at+=(size_t)wrote;}
        psmtp_sha256_update(&hash,h->buffer,n);done+=n;
        if(psmtp_work_progress(h->work,MTP_COPY_RECEIVE,done,m->size)){rc=MTP_CANCELLED;goto end;}
    }
    rc=read_piece(h,h->buffer,1,&n);if(rc)goto end;if(n){rc=MTP_VERIFY;goto end;}
    psmtp_sha256_final(&hash,digest);if(memcmp(digest,m->sha256,32)){rc=MTP_VERIFY;goto end;}
    source=(psmtp_pkg_source){&fd,m->size,header_read};rc=psmtp_pkg_inspect(&source,&pkg);if(rc)goto end;
    if(strcmp(pkg.title_id,PSMTP_TITLE_ID) || strcmp(pkg.content_id,PSMTP_UPDATE_CONTENT_ID)){rc=MTP_VERIFY;goto end;}
    rc=psmtp_native_output_access(fd,0,0,h->log);if(rc)goto end;if(fsync(fd)){rc=MTP_FILESYSTEM;goto end;}
    rc=update_check(h);if(rc)goto end;
    /* Atomic no-replace publication on the internal filesystem. A downloaded
     * file never replaces another PKG, including one with this exact name. */
    if(link(temporary,final)){rc=errno==EEXIST?MTP_EXISTS:MTP_FILESYSTEM;goto end;}published=1;
    snprintf(job->saved_path,sizeof(job->saved_path),"%s",final);rc=fsync(root)?MTP_FILESYSTEM:0;
end:
    if(fd>=0){if(close(fd) && !rc)rc=MTP_FILESYSTEM;if(unlink(temporary) && !rc)rc=MTP_FILESYSTEM;}
    if(root>=0 && close(root) && !rc)rc=MTP_FILESYSTEM;
    if(h->log)fprintf(h->log,"stage=update-download status=%d bytes=%" PRIu64 " expected=%" PRIu64 " published=%d auto-install=0\n",rc,done,m->size,published);return rc;
}
int psmtp_update_run(void *v,psmtp_work *work){
    psmtp_update_job *job=v;update_http *h=NULL;int rc,close_rc;char manifest[PSMTP_UPDATE_MANIFEST_LIMIT+1];size_t used=0,n;
    if(!job || !work)return MTP_STATE;
    if(job->download && (!psmtp_update_newer(&job->manifest,PSMTP_PACKAGE_VERSION) || !psmtp_update_https(job->manifest.url)))return MTP_STATE;
    rc=http_open(&h,job->download?job->manifest.url:PSMTP_UPDATE_URL,work,job->download);if(rc)goto end;
    if(job->download){rc=save_package(h,job);goto end;}
    while(used<sizeof(manifest)-1){rc=read_piece(h,manifest+used,sizeof(manifest)-1-used,&n);if(rc)goto end;if(!n)break;used+=n;}
    if(used==sizeof(manifest)-1){rc=read_piece(h,h->buffer,1,&n);if(rc)goto end;if(n){rc=MTP_LIMIT;goto end;}}
    rc=psmtp_update_parse(manifest,used,&job->manifest);
end:
    if(h){close_rc=http_close(h);if(!rc)rc=close_rc;}
    fprintf(psmtp_work_log(work),"stage=update-check download=%d status=%d newer=%d\n",job->download,rc,!rc && psmtp_update_newer(&job->manifest,PSMTP_PACKAGE_VERSION));return rc;
}
