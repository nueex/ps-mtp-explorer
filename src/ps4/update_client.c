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
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <inttypes.h>
#ifndef PSMTP_TEST_UPDATER
#include "optional_module.h"
#include "monotonic.h"
#include "lan_server.h"
#include <orbis/Net.h>
#include <orbis/NetCtl.h>
#include <orbis/Ssl.h>
#include "psmtp_https_roots.inc"
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
    int32_t (*last_errno)(int32_t,int32_t *);
    int32_t (*ssl_error)(int32_t,int32_t *,uint32_t *);
} update_api;
typedef struct psmtp_https_stream {
    update_api api;int modules[3],owned[3],kernel_owned[3],net_owned,pool,ssl,http,template_id,connection,request;
    int http_status;
    psmtp_work *work;FILE *log;uint64_t deadline;uint8_t buffer[65536];
} update_http;
static update_http *retained_http;
/* The updater and direct downloads can share native module handles, whose
 * lifetime is owned by the first open. Allow one HTTP session at a time. */
static int http_in_use;
#ifdef PSMTP_TEST_UPDATER
static int update_platform_open(update_http *);
static int update_platform_unload(update_http *);
static int update_platform_trust(update_http *);
static uint64_t update_now(void);
static int direct_response_headers(update_http *, char **, size_t *);
#else
static const enum OrbisSysModuleInternal http_modules[]={0,ORBIS_SYSMODULE_INTERNAL_SSL,ORBIS_SYSMODULE_INTERNAL_HTTP};

static uint64_t update_now(void){struct timespec t={0};if(psmtp_monotonic_time(&t))return 0;return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000;}
static int update_platform_open(update_http *h){
    static const char *const names[]={NULL,"libSceSsl","libSceHttp"};
    static const char *const paths[]={NULL,"/system/common/lib/libSceSsl.sprx","/system/common/lib/libSceHttp.sprx"};
    h->api.net_init=sceNetInit;h->api.net_term=sceNetTerm;
    h->api.pool_create=sceNetPoolCreate;h->api.pool_destroy=sceNetPoolDestroy;
    for(unsigned i=1;i<3;++i){
        h->modules[i]=psmtp_optional_module(names[i]);
        if(h->modules[i]>=0)continue;
        int status=(int32_t)sceSysmoduleLoadModuleInternal(http_modules[i]);
        if(h->log)fprintf(h->log,"stage=https-sysmodule name=%s native=0x%08x\n",names[i],(unsigned)status);
        if(!status){
            h->owned[i]=1;
            h->modules[i]=psmtp_optional_module(names[i]);
        }
        if(h->modules[i]<0){
            status=(int32_t)sceKernelLoadStartModule(paths[i],0,NULL,0,NULL,NULL);
            if(h->log)fprintf(h->log,"stage=https-kernel-module name=%s native=0x%08x\n",names[i],(unsigned)status);
            if(status<0)return MTP_UNSUPPORTED;
            h->modules[i]=status;
            h->kernel_owned[i]=1;
        }
    }
#define UP(i,field,name) do{if(psmtp_optional_symbol(h->modules[i],name,&h->api.field,sizeof(h->api.field))){ \
    if(h->log)fprintf(h->log,"stage=https-missing-symbol symbol=%s\n",name);return MTP_UNSUPPORTED;}}while(0)
    UP(1,ssl_init,"sceSslInit");UP(1,ssl_term,"sceSslTerm");UP(2,http_init,"sceHttpInit");UP(2,http_term,"sceHttpTerm");
    UP(2,template_create,"sceHttpCreateTemplate");UP(2,template_delete,"sceHttpDeleteTemplate");UP(2,connect_create,"sceHttpCreateConnectionWithURL");UP(2,connect_delete,"sceHttpDeleteConnection");
    UP(2,request_create,"sceHttpCreateRequestWithURL");UP(2,request_delete,"sceHttpDeleteRequest");UP(2,send,"sceHttpSendRequest");UP(2,status,"sceHttpGetStatusCode");UP(2,read,"sceHttpReadData");
    UP(2,resolve_timeout,"sceHttpSetResolveTimeOut");UP(2,connect_timeout,"sceHttpSetConnectTimeOut");UP(2,send_timeout,"sceHttpSetSendTimeOut");UP(2,recv_timeout,"sceHttpSetRecvTimeOut");
    UP(2,redirect,"sceHttpSetAutoRedirect");UP(2,length,"sceHttpGetResponseContentLength");
    (void)psmtp_optional_symbol(h->modules[2],"sceHttpGetLastErrno",&h->api.last_errno,sizeof(h->api.last_errno));
    (void)psmtp_optional_symbol(h->modules[2],"sceHttpsGetSslError",&h->api.ssl_error,sizeof(h->api.ssl_error));
#undef UP
    return 0;
}
static int update_platform_trust(update_http *h){
    int32_t (*load)(int32_t,int32_t,void *,void *,void *);
    if(psmtp_optional_symbol(h->modules[2],"sceHttpsLoadCert",&load,sizeof(load)))return MTP_UNSUPPORTED;
    int32_t count=(int32_t)(sizeof(https_root_list)/sizeof(*https_root_list));
    int rc=load(h->http,count,https_root_list,NULL,NULL);
    if(h->log)fprintf(h->log,"stage=https-trust-roots count=%d native=0x%08x\n",count,(unsigned)rc);
    return rc;
}
static int update_platform_unload(update_http *h){
    for(int i=2;i>=1;--i){
        if(h->kernel_owned[i]){
            if(sceKernelStopUnloadModule(h->modules[i],0,NULL,0,NULL,NULL))return MTP_IO;
            h->kernel_owned[i]=0;
        }
        if(h->owned[i]){
            if(sceSysmoduleUnloadModuleInternal(http_modules[i]))return MTP_IO;
            h->owned[i]=0;
        }
    }
    return 0;
}
#endif
static int online_failure(update_http *h,const char *operation,int native,int fallback){
    int32_t network_error=0;
    if(h->api.last_errno && h->request>=0)(void)h->api.last_errno(h->request,&network_error);
    if(h->log)fprintf(h->log,"stage=https-failure operation=%s native=0x%08x network-errno=%d\n",operation,(unsigned)native,network_error);
    if(h->log && h->api.ssl_error && h->request>=0){
        int32_t ssl_error=0;
        uint32_t verify_flags=0;
        int rc=h->api.ssl_error(h->request,&ssl_error,&verify_flags);
        if(!rc)fprintf(h->log,"stage=https-certificate native=0x%08x verify=0x%08x\n",(unsigned)ssl_error,(unsigned)verify_flags);
    }
    if(network_error==ENETDOWN || network_error==ENETUNREACH || network_error==ENETRESET)return MTP_NO_INTERNET;
#ifndef PSMTP_TEST_UPDATER
    if(!sceNetCtlInit()){
        OrbisNetCtlInfo ip={0},route={0};
        int ip_status=sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_IP_ADDRESS,&ip);
        int route_status=sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_DEFAULT_ROUTE,&route);
        sceNetCtlTerm();
        if(ip_status || !ip.ip_address[0] || !strcmp(ip.ip_address,"0.0.0.0") ||
           (!route_status && (!route.default_route[0] || !strcmp(route.default_route,"0.0.0.0"))))
            return MTP_NO_INTERNET;
    }
#endif
    return fallback;
}
static int update_check(update_http *h){uint64_t now;if(psmtp_work_cancelled(h->work))return MTP_CANCELLED;now=update_now();return !now || now>=h->deadline?MTP_TIMEOUT:0;}
static int http_close(update_http *h){int rc=0;
#define DROP(field,fn) do{if(h->field>=0){if(h->api.fn(h->field)){rc=MTP_IO;goto done;}h->field=-1;}}while(0)
    DROP(request,request_delete);DROP(connection,connect_delete);DROP(template_id,template_delete);DROP(http,http_term);DROP(ssl,ssl_term);
#undef DROP
    if(h->pool>=0){h->api.pool_destroy(h->pool);h->pool=-1;}if(h->net_owned){
#ifdef PSMTP_TEST_UPDATER
        h->api.net_term();
#else
        psmtp_lan_net_release(h->api.net_term);
#endif
        h->net_owned=0;
    }
    rc=update_platform_unload(h);
done:
    if(h->log)fprintf(h->log,"stage=update-http-close status=%d retained=%d\n",rc,rc!=0);
    if(rc)retained_http=h;
    else {free(h);__atomic_store_n(&http_in_use,0,__ATOMIC_RELEASE);}
    return rc;
}
static int http_open(update_http **out,const char *url,psmtp_work *work,int download,const psmtp_download_link *link){
    update_http *h;int rc;uint64_t now=update_now();*out=NULL;
    if(!now || (!link && !psmtp_update_https(url)))return MTP_UNSUPPORTED;
    if(__atomic_exchange_n(&http_in_use,1,__ATOMIC_ACQ_REL))return MTP_STATE;
    h=calloc(1,sizeof(*h));
    if(!h){__atomic_store_n(&http_in_use,0,__ATOMIC_RELEASE);return MTP_MEMORY;}
    *out=h;h->work=work;h->log=psmtp_work_log(work);h->pool=h->ssl=h->http=h->template_id=h->connection=h->request=-1;
    h->deadline=now+(link?UINT64_C(30000):download?UINT64_C(1800000):UINT64_C(8000));
    rc=update_platform_open(h);if(rc)return online_failure(h,"load-modules",rc,rc);
    /* Each client owns its pool and TLS context. The network stack is shared with LAN and the installer. */
#ifdef PSMTP_TEST_UPDATER
    rc=h->api.net_init();
#else
    rc=psmtp_lan_net_acquire(h->api.net_init);
#endif
    if(rc)return online_failure(h,"initialize-network",rc,MTP_IO);h->net_owned=1;
    h->pool=h->api.pool_create("psmtp-update",2*1024*1024,0);if(h->pool<0)return online_failure(h,"create-pool",h->pool,MTP_MEMORY);
    h->ssl=h->api.ssl_init(4*1024*1024);if(h->ssl<0)return online_failure(h,"initialize-ssl",h->ssl,MTP_IO);
    h->http=h->api.http_init(h->pool,h->ssl,2*1024*1024);if(h->http<0)return online_failure(h,"initialize-http",h->http,MTP_IO);
    rc=update_platform_trust(h);if(rc)return online_failure(h,"load-trust-roots",rc,MTP_IO);
    h->template_id=h->api.template_create(h->http,link && *link->agent ? link->agent : "PS-MTP-Explorer/" PSMTP_VERSION,2,0);if(h->template_id<0)return online_failure(h,"create-template",h->template_id,MTP_IO);
    /* Native TLS verification stays enabled. Link redirects are checked before
     * each request so native auto-redirect cannot forward credentials. */
    unsigned initial_timeout=link?7000000u:2000000u;
    if(h->api.redirect(h->template_id,0) || h->api.resolve_timeout(h->template_id,initial_timeout) ||
       h->api.connect_timeout(h->template_id,initial_timeout) || h->api.send_timeout(h->template_id,initial_timeout) ||
       h->api.recv_timeout(h->template_id,link?12000000u:download?5000000u:2000000u))return MTP_IO;
    rc=update_check(h);if(rc)return rc;
    /* The persistent connection path retains the context's custom trust. */
    h->connection=h->api.connect_create(h->template_id,url,1);if(h->connection<0)return online_failure(h,"create-connection",h->connection,MTP_IO);
    h->request=h->api.request_create(h->connection,0,url,0);if(h->request<0)return online_failure(h,"create-request",h->request,MTP_IO);
#ifndef PSMTP_TEST_UPDATER
    if (link) {
        int32_t (*add)(int32_t, const char *, const char *, int32_t);
        if (psmtp_optional_symbol(h->modules[2], "sceHttpAddRequestHeader", &add, sizeof(add))) {
            if(h->log)fputs("stage=https-missing-symbol symbol=sceHttpAddRequestHeader\n",h->log);
            return online_failure(h,"resolve-request-header",0,MTP_UNSUPPORTED);
        }
        if (add(h->request, "Accept-Encoding", "identity", 0) ||
            (*link->referer && add(h->request, "Referer", link->referer, 0)) ||
            (*link->cookie && add(h->request, "Cookie", link->cookie, 0))) return MTP_IO;
    }
#endif
    rc=h->api.send(h->request,NULL,0);if(h->log)fprintf(h->log,"stage=update-http-send download=%d native=0x%08x\n",download,(unsigned)rc);if(rc)return online_failure(h,"send-request",rc,MTP_IO);
    if(h->api.status(h->request,&h->http_status))return MTP_IO;
    if(h->log)fprintf(h->log,"stage=update-http-status download=%d code=%d tls-verification=bundled-roots\n",download,h->http_status);
    if(h->http_status!=200)return MTP_RESPONSE;
    return update_check(h);
}
static int read_piece(update_http *h,void *bytes,size_t cap,size_t *got){int n,rc=update_check(h);*got=0;if(rc)return rc;n=h->api.read(h->request,bytes,(uint32_t)cap);if(n<0)return online_failure(h,"read-response",n,MTP_IO);if((unsigned)n>cap)return MTP_PROTOCOL;*got=(size_t)n;return update_check(h);}
/* Match a single response header token without reading past the native buffer. */
static int header_token(const char *at, size_t length, const char *key, const char *token) {
    const char *end = at + length;
    size_t key_size = strlen(key), token_size = token ? strlen(token) : 0;
    while (at < end) {
        const char *lf = memchr(at, '\n', (size_t)(end - at));
        const char *line_end = lf ? lf : end;
        if ((size_t)(line_end - at) > key_size && at[key_size] == ':' && !strncasecmp(at, key, key_size)) {
            at += key_size + 1;
            while (at < line_end && (*at == ' ' || *at == '\t')) ++at;
            const char *value = at;
            while (at < line_end && *at != ';' && *at != '\r' && *at != ' ' && *at != '\t') ++at;
            if (!token || ((size_t)(at - value) == token_size && !strncasecmp(value, token, token_size))) return 1;
        }
        at = lf ? lf + 1 : end;
    }
    return 0;
}

static int link_response_valid(const char *headers, size_t length) {
    if (header_token(headers, length, "Content-Encoding", NULL) &&
        !header_token(headers, length, "Content-Encoding", "identity")) return 0;
    return header_token(headers, length, "Content-Disposition", "attachment") ||
        !(header_token(headers, length, "Content-Type", "text/html") ||
          header_token(headers, length, "Content-Type", "application/xhtml+xml"));
}

static int direct_link_valid(const char *url) {
    if (strncmp(url, "https://", 8) || strlen(url) >= PSMTP_LINK_MAX) return 0;
    const char *host = url + 8;
    size_t authority = strcspn(host, "/?#");
    if (!authority || memchr(host, '@', authority)) return 0;
    for (const unsigned char *at = (const unsigned char *)url; *at; ++at)
        if (*at <= 32 || *at >= 127 || *at == '#' || *at == '\\') return 0;
    return 1;
}

#ifndef PSMTP_TEST_UPDATER
static int direct_response_headers(update_http *h, char **headers, size_t *length) {
    int32_t (*get_headers)(int32_t, char **, size_t *);
    if (psmtp_optional_symbol(h->modules[2], "sceHttpGetAllResponseHeaders", &get_headers, sizeof(get_headers)) ||
        get_headers(h->request, headers, length) || !*headers) return MTP_IO;
    return 0;
}
#endif

static char *redirect_url(const char *current, const char *headers, size_t length) {
    const char *end = headers + length;
    while (headers < end) {
        const char *newline = memchr(headers, '\n', (size_t)(end - headers));
        const char *line_end = newline ? newline : end;
        if (line_end - headers > 9 && !strncasecmp(headers, "Location:", 9)) {
            const char *value = headers + 9;
            while (value < line_end && (*value == ' ' || *value == '\t')) ++value;
            while (line_end > value && (line_end[-1] == '\r' || line_end[-1] == ' ' || line_end[-1] == '\t'))
                --line_end;
            const char *fragment = memchr(value, '#', (size_t)(line_end - value));
            if (fragment) line_end = fragment;
            if (value == line_end) return NULL;
            size_t prefix, value_size = (size_t)(line_end - value);
            if (memchr(value, 0, value_size)) return NULL;
            size_t path = 8 + strcspn(current + 8, "/?");
            if (value_size >= 8 && !memcmp(value, "https://", 8)) prefix = 0;
            else if (value_size >= 2 && value[0] == '/' && value[1] == '/') prefix = 6;
            else if (value[0] == '/') prefix = path;
            else if (value[0] == '?') prefix = strcspn(current, "?");
            else {
                size_t part = 0;
                while (part < value_size && value[part] != '/' && value[part] != '?') {
                    if (value[part] == ':') return NULL;
                    ++part;
                }
                prefix = strcspn(current, "?");
                while (prefix > path && current[prefix - 1] != '/') --prefix;
            }
            int add_slash = prefix == path && value[0] != '/' && value[0] != '?';
            if (prefix + add_slash + value_size >= PSMTP_LINK_MAX) return NULL;
            char *next = malloc(prefix + add_slash + value_size + 1);
            if (!next) return NULL;
            memcpy(next, current, prefix);
            if (add_slash) next[prefix++] = '/';
            memcpy(next + prefix, value, value_size);
            next[prefix + value_size] = 0;
            if (direct_link_valid(next)) return next;
            free(next);
            return NULL;
        }
        headers = newline ? newline + 1 : end;
    }
    return NULL;
}

static int same_link_origin(const char *first, const char *second) {
    size_t a = strcspn(first + 8, "/?"), b = strcspn(second + 8, "/?");
    return a == b && !strncasecmp(first + 8, second + 8, a);
}

int psmtp_https_open_link(psmtp_https_stream **out, const psmtp_download_link *link, psmtp_work *work, uint64_t *size) {
    *out = NULL;
    if (!direct_link_valid(link->url)) return MTP_PROTOCOL;
    int rc;
    psmtp_download_link request = *link;
    char *owned_url = NULL;
    uint64_t started = update_now();
    for (unsigned attempt = 0;; ++attempt) {
        rc = http_open(out, request.url, work, 1, &request);
        int status = psmtp_https_http_status(*out);
        if (rc && (rc != MTP_RESPONSE || (status != 301 && status != 302 && status != 303 &&
                                         status != 307 && status != 308))) break;
        char *headers = NULL;
        size_t length = 0;
        if (direct_response_headers(*out, &headers, &length)) {rc = MTP_IO;break;}
        if (!rc) {
            if (!link_response_valid(headers, length)) rc = MTP_VERIFY;
            break;
        }
        if (attempt >= 4 || update_now() - started >= 30000) break;
        char *next = redirect_url(request.url, headers, length);
        if (!next) break;
        if (!same_link_origin(request.url, next)) request.cookie = request.referer = "";
        rc = http_close(*out);
        *out = NULL;
        if (rc) {free(next);break;}
        free(owned_url);
        request.url = owned_url = next;
    }
    free(owned_url);
    if (rc) return rc;
    update_http *h = *out;
    int type = -1;
    uint64_t length = 0;
    if (!h->api.length(h->request, &type, &length) && type == 0) {
        if (length > PSMTP_DOWNLOAD_MAX || (*size != UINT64_MAX && length != *size)) return MTP_VERIFY;
        *size = length;
    }
    h->deadline = UINT64_MAX;
    return MTP_OK;
}

int psmtp_https_http_status(const psmtp_https_stream *h) { return h ? h->http_status : 0; }

int psmtp_https_read(psmtp_https_stream *h, void *bytes, size_t capacity, size_t *read) {
    return read_piece(h, bytes, capacity, read);
}

int psmtp_https_close(psmtp_https_stream *h) {
    return http_close(h);
}

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
    rc=http_open(&h,job->download?job->manifest.url:PSMTP_UPDATE_URL,work,job->download,NULL);if(rc)goto end;
    if(job->download){rc=save_package(h,job);goto end;}
    while(used<sizeof(manifest)-1){rc=read_piece(h,manifest+used,sizeof(manifest)-1-used,&n);if(rc)goto end;if(!n)break;used+=n;}
    if(used==sizeof(manifest)-1){rc=read_piece(h,h->buffer,1,&n);if(rc)goto end;if(n){rc=MTP_LIMIT;goto end;}}
    rc=psmtp_update_parse(manifest,used,&job->manifest);
end:
    if(h){close_rc=http_close(h);if(!rc)rc=close_rc;}
    fprintf(psmtp_work_log(work),"stage=update-check download=%d status=%d newer=%d\n",job->download,rc,!rc && psmtp_update_newer(&job->manifest,PSMTP_PACKAGE_VERSION));return rc;
}
