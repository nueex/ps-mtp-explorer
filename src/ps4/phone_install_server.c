/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#include "phone_install_server.h"
#include "psmtp/http_range.h"
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#ifdef PSMTP_TEST_PHONE_SERVER
#include "ps4_phone_server_test_api.h"
#else
#include <orbis/libkernel.h>
#include <orbis/Net.h>
#include <orbis/Sysmodule.h>
#include "monotonic.h"
#include "lifecycle.h"
#endif
#include "native_sync.h"
#define PHONE_CLIENTS 4u
#define PHONE_CHUNK 65536u
#define PHONE_MAX_CHUNK MTP_PARTIAL_BUFFER_BYTES
#define PHONE_QUEUE_MAX 4u
#define PHONE_SOCKET_WAIT_MS 5000u
#define PHONE_TRACE_QUEUE 16u
#define PHONE_TRACE_LIMIT 64u
/* libSceNet socket options: integer nonblocking mode, no timeval ABI guesses.
 * Reads/writes/accept are polled with bounded inactivity and cancellation. */
#define PHONE_SOL_SOCKET 0xffff
#define PHONE_SO_NBIO 0x1200
typedef struct {
    int32_t (*init)(void);void (*term)(void);
    int32_t (*pool_create)(const char *,int32_t,int32_t);void (*pool_destroy)(int32_t);
    int32_t (*socket)(const char *,int32_t,int32_t,int),(*close)(int32_t),(*abort)(int32_t,int);
    int32_t (*bind)(int32_t,const OrbisNetSockaddr *,OrbisNetSocklen_t),(*listen)(int32_t,int);
    int32_t (*accept)(int32_t,OrbisNetSockaddr *,OrbisNetSocklen_t *),(*name)(int32_t,OrbisNetSockaddr *,OrbisNetSocklen_t *);
    int32_t (*connect)(int32_t,const OrbisNetSockaddr *,OrbisNetSocklen_t);
    int32_t (*option)(int32_t,int32_t,int32_t,const void *,OrbisNetSocklen_t);
    int32_t (*recv)(int32_t,void *,size_t,int),(*send)(int32_t,const void *,size_t,int);
} phone_net;
typedef struct {
    struct psmtp_phone_server *server;OrbisPthread thread;unsigned index;
    int started,done,joined,fd,active,fetching,finished,blocked;
    unsigned head,tail,high_water;uint64_t offset,remaining;
    size_t lengths[PHONE_QUEUE_MAX];uint8_t *buffers;
    char header[PSMTP_HTTP_HEADER_LIMIT+1];
} phone_slot;
typedef struct {unsigned serial,worker;int phase,status,rc,head,health;uint64_t offset,length;char line[192];} phone_trace;
struct psmtp_phone_server {
    phone_net net;psmtp_phone_source source;phone_slot slots[PHONE_CLIENTS];psmtp_sync io,state;
    int listener,probe,pool,module_loaded,net_owned,stop,hard_cancel,error,hooks,close_failed;unsigned requests,epoch;
    uint64_t pause_total;char path[96];
    phone_trace traces[PHONE_TRACE_QUEUE];unsigned trace_head,trace_count,trace_serial,trace_dropped,connections;uint64_t sent_bytes;
    size_t chunk_bytes;uint64_t read_calls,read_bytes,read_ms,io_wait_ms,send_ms;
    unsigned queue_slots,reader_cursor;OrbisPthread reader;int reader_started,reader_joined;
    void *old_control;int (*old_checkpoint)(void *);uint64_t (*old_paused)(void *);int old_quiet;
};
static uint64_t phone_now(void){
#ifdef PSMTP_TEST_PHONE_SERVER
    return phone_test_now();
#else
    struct timespec t={0};return psmtp_monotonic_time(&t)?0:(uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000;
#endif
}
static void phone_error(psmtp_phone_server *s,int rc){int expected=0;if(rc)(void)__atomic_compare_exchange_n(&s->error,&expected,rc,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);}
int psmtp_phone_server_error(psmtp_phone_server *s){return s?__atomic_load_n(&s->error,__ATOMIC_ACQUIRE):0;}
unsigned psmtp_phone_server_requests(psmtp_phone_server *s){return s?__atomic_load_n(&s->requests,__ATOMIC_ACQUIRE):0;}
static void trace_request(phone_slot *slot,const psmtp_http_request *r,int phase,int rc){psmtp_phone_server *s=slot->server;phone_trace *t;size_t i;
    if(psmtp_sync_lock(&s->state))return;
    if(s->trace_serial>=PHONE_TRACE_LIMIT || s->trace_count==PHONE_TRACE_QUEUE){++s->trace_dropped;psmtp_sync_unlock(&s->state);return;}
    t=&s->traces[(s->trace_head+s->trace_count++)%PHONE_TRACE_QUEUE];memset(t,0,sizeof(*t));t->serial=++s->trace_serial;t->worker=slot->index;t->phase=phase;t->rc=rc;
    if(r){t->status=r->status;t->head=r->head;t->health=r->health;t->offset=r->offset;t->length=r->length;}
    for(i=0;i<sizeof(t->line)-1 && slot->header[i] && slot->header[i]!='\r' && slot->header[i]!='\n';++i){unsigned char c=(unsigned char)slot->header[i];t->line[i]=(c>=32 && c<127 && c!='"')?(char)c:'?';}
    psmtp_sync_unlock(&s->state);
}
void psmtp_phone_server_log(psmtp_phone_server *s,FILE *log){unsigned i;if(!s || !log || !s->state.ready)return;
    for(i=0;i<PHONE_TRACE_QUEUE;++i){phone_trace t;if(psmtp_sync_lock(&s->state))return;if(!s->trace_count){psmtp_sync_unlock(&s->state);break;}
        t=s->traces[s->trace_head];s->trace_head=(s->trace_head+1)%PHONE_TRACE_QUEUE;--s->trace_count;psmtp_sync_unlock(&s->state);
        fprintf(log,"stage=phone-http request=%u worker=%u phase=%d status=%d app-status=%d head=%d health=%d offset=%" PRIu64 " bytes=%" PRIu64 " line=\"%s\"\n",t.serial,t.worker,t.phase,t.status,t.rc,t.head,t.health,t.offset,t.length,t.line);
    }
}
static int phone_control(psmtp_phone_server *s,int *pause,unsigned *epoch){int rc;*pause=0;*epoch=0;
    if(__atomic_load_n(&s->hard_cancel,__ATOMIC_ACQUIRE))return MTP_CANCELLED;
    rc=s->source.control?s->source.control(s->source.control_user,pause,epoch):0;return rc;
}
/* Called only while the serialized source I/O mutex is held. Never send MTP
 * metadata commands from inside a paused/in-flight protocol transaction. */
static int phone_checkpoint(void *user){psmtp_phone_server *s=user;int pause,rc;unsigned epoch;uint64_t start=0;
    for(;;){rc=phone_control(s,&pause,&epoch);if(rc)return rc;if(!pause)break;if(!start)start=phone_now();(void)sceKernelUsleep(10000);}
    if(start){uint64_t now=phone_now();if(now>=start)s->pause_total+=now-start;}
    if(epoch!=s->epoch){mtp_client *c=s->source.client;rc=c->resume_check?c->resume_check(c->io.user):MTP_UNSUPPORTED;if(rc)return rc;s->epoch=epoch;}
    return 0;
}
static uint64_t phone_paused(void *user){return ((psmtp_phone_server *)user)->pause_total;}
static int wire_stopped(psmtp_phone_server *s){int pause;unsigned epoch;return __atomic_load_n(&s->stop,__ATOMIC_ACQUIRE) || psmtp_phone_server_error(s) || phone_control(s,&pause,&epoch)!=0;}
static int wire_send(psmtp_phone_server *s,int fd,const void *data,size_t length){const uint8_t *bytes=data;size_t at=0;uint64_t last=phone_now();
    while(at<length){int32_t n;if(wire_stopped(s))return MTP_CANCELLED;n=s->net.send(fd,bytes+at,length-at,0x20000);
        if(n>0){if((size_t)n>length-at)return MTP_PROTOCOL;at+=(size_t)n;last=phone_now();}
        else {if(n==0 || phone_now()-last>=PHONE_SOCKET_WAIT_MS)return MTP_IO;(void)sceKernelUsleep(1000);}}
    return 0;
}
static int wire_header(psmtp_phone_server *s,int fd,char *bytes,size_t cap,size_t *length){uint64_t start=phone_now();size_t n=0;
    while(n<cap-1){int32_t got;if(wire_stopped(s))return MTP_CANCELLED;got=s->net.recv(fd,bytes+n,cap-1-n,0);
        if(got>0){char *end;if((size_t)got>cap-1-n)return MTP_PROTOCOL;n+=(size_t)got;bytes[n]=0;end=strstr(bytes,"\r\n\r\n");if(end){*length=(size_t)(end-bytes)+4;return 0;}}
        else {if(!got || phone_now()-start>=PHONE_SOCKET_WAIT_MS)return MTP_IO;(void)sceKernelUsleep(1000);}}
    return MTP_LIMIT;
}
static int nonblock(psmtp_phone_server *s,int fd){int on=1;return s->net.option(fd,PHONE_SOL_SOCKET,PHONE_SO_NBIO,&on,sizeof(on))?MTP_IO:0;}
static void *buffer_alloc(size_t n){
#ifdef PSMTP_TEST_PHONE_SERVER
    return phone_test_buffer_alloc(n);
#else
    return malloc(n);
#endif
}
static void buffer_free(void *p){
#ifdef PSMTP_TEST_PHONE_SERVER
    phone_test_buffer_free(p);
#else
    free(p);
#endif
}
/* One source producer feeds bounded per-connection queues. HTTP consumers
 * send a published block while the producer fills a different free block.
 * Source reads and metadata validation share the same MTP mutex. */
static void *phone_reader(void *user){psmtp_phone_server *s=user;
    while(!wire_stopped(s)){phone_slot *slot=NULL;unsigned i,index=0;size_t length=0;uint64_t offset=0,before,started,finished;int rc;
        if(psmtp_sync_lock(&s->state)){phone_error(s,MTP_IO);break;}
        for(i=0;i<PHONE_CLIENTS;++i){unsigned n=(s->reader_cursor+i)%PHONE_CLIENTS;phone_slot *candidate=&s->slots[n];
            if(candidate->active && !candidate->fetching && candidate->remaining && candidate->head-candidate->tail<s->queue_slots){slot=candidate;s->reader_cursor=(n+1)%PHONE_CLIENTS;break;}}
        if(!slot){rc=psmtp_sync_wait(&s->state,50000);psmtp_sync_unlock(&s->state);if(rc){phone_error(s,MTP_IO);break;}continue;}
        slot->fetching=1;index=slot->head%s->queue_slots;offset=slot->offset;length=(size_t)(slot->remaining>s->chunk_bytes?s->chunk_bytes:slot->remaining);psmtp_sync_unlock(&s->state);
        before=phone_now();rc=psmtp_sync_lock(&s->io);started=phone_now();if(started>=before)__atomic_fetch_add(&s->io_wait_ms,started-before,__ATOMIC_RELAXED);
        if(!rc){rc=phone_checkpoint(s);if(!rc && !__atomic_load_n(&s->stop,__ATOMIC_ACQUIRE))rc=s->source.read(s->source.user,offset,slot->buffers+(size_t)index*s->chunk_bytes,length);else if(!rc)rc=MTP_CANCELLED;psmtp_sync_unlock(&s->io);}else rc=MTP_IO;
        finished=phone_now();__atomic_fetch_add(&s->read_calls,1,__ATOMIC_RELAXED);if(finished>=started)__atomic_fetch_add(&s->read_ms,finished-started,__ATOMIC_RELAXED);if(!rc)__atomic_fetch_add(&s->read_bytes,(uint64_t)length,__ATOMIC_RELAXED);
        if(psmtp_sync_lock(&s->state)){phone_error(s,MTP_IO);break;}
        slot->fetching=0;if(!rc && slot->active){slot->lengths[index]=length;slot->offset+=length;slot->remaining-=length;++slot->head;if(slot->head-slot->tail>slot->high_water)slot->high_water=slot->head-slot->tail;if(!slot->remaining)slot->finished=1;}
        if(rc){slot->finished=1;if(!__atomic_load_n(&s->stop,__ATOMIC_ACQUIRE))phone_error(s,rc);}psmtp_sync_signal_locked(&s->state);psmtp_sync_unlock(&s->state);
    }return NULL;
}
static int queued_body(phone_slot *slot,const psmtp_http_request *request){psmtp_phone_server *s=slot->server;int rc=0;
    if(psmtp_sync_lock(&s->state))return MTP_IO;slot->head=slot->tail=0;slot->offset=request->offset;slot->remaining=request->length;slot->finished=request->length==0;slot->active=1;psmtp_sync_signal_locked(&s->state);psmtp_sync_unlock(&s->state);
    for(;;){unsigned index;size_t length;uint64_t before,after;
        if(wire_stopped(s)){rc=MTP_CANCELLED;break;}rc=psmtp_phone_server_error(s);if(rc)break;
        if(psmtp_sync_lock(&s->state)){rc=MTP_IO;break;}
        if(slot->head==slot->tail){if(slot->finished){psmtp_sync_unlock(&s->state);break;}rc=psmtp_sync_wait(&s->state,50000);psmtp_sync_unlock(&s->state);if(rc){rc=MTP_IO;break;}continue;}
        index=slot->tail%s->queue_slots;length=slot->lengths[index];psmtp_sync_unlock(&s->state);
        before=phone_now();rc=wire_send(s,slot->fd,slot->buffers+(size_t)index*s->chunk_bytes,length);after=phone_now();if(after>=before)__atomic_fetch_add(&s->send_ms,after-before,__ATOMIC_RELAXED);if(rc)break;
        __atomic_fetch_add(&s->sent_bytes,(uint64_t)length,__ATOMIC_RELAXED);
        if(psmtp_sync_lock(&s->state)){rc=MTP_IO;break;}++slot->tail;psmtp_sync_signal_locked(&s->state);psmtp_sync_unlock(&s->state);
    }
    /* An abandoned HTTP request cannot recycle a buffer still being filled.
     * Wait at most the existing I/O deadline, then retain it for safe exit. */
    {uint64_t deadline=phone_now()+PHONE_SOCKET_WAIT_MS;
    if(psmtp_sync_lock(&s->state))return MTP_IO;slot->active=0;psmtp_sync_signal_locked(&s->state);
    while(slot->fetching){if(phone_now()>=deadline){slot->blocked=1;psmtp_sync_unlock(&s->state);phone_error(s,MTP_TIMEOUT);psmtp_phone_server_stop(s,1);return MTP_TIMEOUT;}
        if(psmtp_sync_wait(&s->state,50000)){slot->blocked=1;psmtp_sync_unlock(&s->state);phone_error(s,MTP_IO);psmtp_phone_server_stop(s,1);return MTP_IO;}}
    psmtp_sync_unlock(&s->state);}return rc;
}
static void slot_close(phone_slot *slot){psmtp_phone_server *s=slot->server;if(psmtp_sync_lock(&s->state)){phone_error(s,MTP_IO);return;}
    if(slot->fd>=0){int fd=slot->fd;slot->fd=-1;if(s->net.close(fd))__atomic_store_n(&s->close_failed,1,__ATOMIC_RELEASE);}psmtp_sync_unlock(&s->state);
}
static void serve(phone_slot *slot){psmtp_phone_server *s=slot->server;psmtp_http_request request;size_t header_size=0;int rc,n;char reply[512];
    slot->header[0]=0;
    rc=wire_header(s,slot->fd,slot->header,sizeof(slot->header),&header_size);
    if(rc){trace_request(slot,NULL,1,rc);if(rc==MTP_LIMIT){memset(&request,0,sizeof(request));request.status=431;n=psmtp_http_response(reply,sizeof(reply),&request);if(n>0)(void)wire_send(s,slot->fd,reply,(size_t)n);}return;}
    (void)psmtp_http_parse(slot->header,header_size,s->path,s->source.size,&request);
    trace_request(slot,&request,0,0);
    if((request.status==200 || request.status==206) && !request.health){
        if(psmtp_sync_lock(&s->io)){phone_error(s,MTP_IO);return;}
        rc=phone_checkpoint(s);if(!rc && s->source.validate)rc=s->source.validate(s->source.user);
        psmtp_sync_unlock(&s->io);if(rc){phone_error(s,rc);return;}
        __atomic_fetch_add(&s->requests,1,__ATOMIC_RELAXED);
    }
    n=psmtp_http_response(reply,sizeof(reply),&request);rc=n<0?MTP_LIMIT:wire_send(s,slot->fd,reply,(size_t)n);if(rc){trace_request(slot,&request,2,rc);return;}
    if(request.head || request.health || (request.status!=200 && request.status!=206))return;
    rc=queued_body(slot,&request);if(rc){trace_request(slot,&request,3,rc);return;}
    if(psmtp_sync_lock(&s->io)){phone_error(s,MTP_IO);return;}
    rc=s->source.validate?s->source.validate(s->source.user):0;psmtp_sync_unlock(&s->io);if(rc)phone_error(s,rc);
}
static void *phone_worker(void *user){phone_slot *slot=user;psmtp_phone_server *s=slot->server;
    while(!wire_stopped(s)){int fd=s->net.accept(s->listener,NULL,NULL);
        if(fd<0){(void)sceKernelUsleep(10000);continue;}
        __atomic_fetch_add(&s->connections,1,__ATOMIC_RELAXED);
        if(psmtp_sync_lock(&s->state)){(void)s->net.close(fd);phone_error(s,MTP_IO);break;}slot->fd=fd;psmtp_sync_unlock(&s->state);
        if(!nonblock(s,fd) && !wire_stopped(s))serve(slot);if(slot->blocked)break;slot_close(slot);
    }
    __atomic_store_n(&slot->done,1,__ATOMIC_RELEASE);return NULL;
}
#ifndef PSMTP_TEST_PHONE_SERVER
static int net_module(void){OrbisKernelModule ids[128];size_t count=0,i;int found=-1;if(sceKernelGetModuleList(ids,128,&count) || count>128)return -1;
    for(i=0;i<count;++i){OrbisKernelModuleInfo info;const char *name;memset(&info,0,sizeof(info));info.size=sizeof(info);if(ids[i]>INT32_MAX || sceKernelGetModuleInfo(ids[i],&info))return -1;
        if(strnlen(info.name,sizeof(info.name))==sizeof(info.name))return -1;name=strrchr(info.name,'/');name=name?name+1:info.name;
        if(!strcmp(name,"libSceNet") || !strcmp(name,"libSceNet.sprx") || !strcmp(name,"libSceNet.prx")){if(found>=0)return -1;found=(int)ids[i];}}
    return found;
}
static int net_symbol(int module,const char *name,void *out,size_t n){void *address=NULL;if(module<0 || n!=sizeof(address) || sceKernelDlsym(module,name,&address) || !address)return MTP_UNSUPPORTED;memcpy(out,&address,n);return 0;}
#endif
static int net_load(psmtp_phone_server *s){
#ifdef PSMTP_TEST_PHONE_SERVER
    return phone_test_net_load(&s->net,&s->module_loaded);
#else
    int module,rc=sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET);if(rc)return MTP_UNSUPPORTED;s->module_loaded=1;module=net_module();
#define NET(field,name) do{if(0)s->net.field=&name;if(net_symbol(module,#name,&s->net.field,sizeof(s->net.field)))return MTP_UNSUPPORTED;}while(0)
    NET(init,sceNetInit);NET(term,sceNetTerm);NET(pool_create,sceNetPoolCreate);NET(pool_destroy,sceNetPoolDestroy);
    NET(socket,sceNetSocket);NET(close,sceNetSocketClose);NET(abort,sceNetSocketAbort);NET(bind,sceNetBind);NET(listen,sceNetListen);
    NET(accept,sceNetAccept);NET(name,sceNetGetsockname);NET(connect,sceNetConnect);NET(option,sceNetSetsockopt);NET(recv,sceNetRecv);NET(send,sceNetSend);
#undef NET
    return 0;
#endif
}
static void address(uint8_t bytes[16],unsigned port){memset(bytes,0,16);bytes[0]=16;bytes[1]=2;bytes[2]=(uint8_t)(port>>8);bytes[3]=(uint8_t)port;bytes[4]=127;bytes[7]=1;}
static int health(psmtp_phone_server *s,const uint8_t addr[16]){char request[256],reply[1024];size_t length=0;int rc,n,fd=s->net.socket("psmtp-phone-check",2,1,0);if(fd<0)return MTP_IO;
    if(psmtp_sync_lock(&s->state)){s->net.close(fd);return MTP_IO;}s->probe=fd;psmtp_sync_unlock(&s->state);
    rc=nonblock(s,fd);if(!rc){(void)s->net.connect(fd,(const OrbisNetSockaddr *)addr,16);
        n=snprintf(request,sizeof(request),"HEAD %s/health HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n",s->path);
        rc=n<0 || (size_t)n>=sizeof(request)?MTP_LIMIT:wire_send(s,fd,request,(size_t)n);if(!rc)rc=wire_header(s,fd,reply,sizeof(reply),&length);
        if(!rc && (length<12 || memcmp(reply,"HTTP/1.1 200",12) || !strstr(reply,"\r\nX-PSMTP-Bridge: ready\r\n")))rc=MTP_PROTOCOL;}
    if(psmtp_sync_lock(&s->state))return MTP_IO;s->probe=-1;if(s->net.close(fd)){__atomic_store_n(&s->close_failed,1,__ATOMIC_RELEASE);rc=MTP_IO;}psmtp_sync_unlock(&s->state);return rc;
}
void psmtp_phone_server_stop(psmtp_phone_server *s,int hard){unsigned i;if(!s)return;__atomic_store_n(&s->stop,1,__ATOMIC_RELEASE);if(hard)__atomic_store_n(&s->hard_cancel,1,__ATOMIC_RELEASE);
    psmtp_sync_wake(&s->state);
    if(!s->state.ready || !s->net.abort || psmtp_sync_trylock(&s->state))return;
    if(s->listener>=0)(void)s->net.abort(s->listener,0);if(s->probe>=0)(void)s->net.abort(s->probe,0);
    for(i=0;i<PHONE_CLIENTS;++i)if(s->slots[i].fd>=0)(void)s->net.abort(s->slots[i].fd,0);psmtp_sync_unlock(&s->state);
}
int psmtp_phone_server_close(psmtp_phone_server **owner,int hard,FILE *log){psmtp_phone_server *s=owner?*owner:NULL;unsigned i;int rc;if(!s)return 0;psmtp_phone_server_stop(s,hard);
    for(i=0;i<PHONE_CLIENTS;++i){phone_slot *slot=&s->slots[i];if(!slot->started || slot->joined)continue;
#ifdef PSMTP_TEST_PHONE_SERVER
        rc=phone_test_join(slot->thread,250);
#else
        rc=psmtp_lifecycle_join((void *)slot->thread,NULL,250);
#endif
        if(rc)return MTP_TIMEOUT;slot->joined=1;
    }
    if(s->reader_started && !s->reader_joined){
#ifdef PSMTP_TEST_PHONE_SERVER
        rc=phone_test_join(s->reader,250);
#else
        rc=psmtp_lifecycle_join((void *)s->reader,NULL,250);
#endif
        if(rc)return MTP_TIMEOUT;s->reader_joined=1;
    }
    /* A failed close has ambiguous descriptor ownership. Retain everything
     * for process teardown rather than retry a possibly reused socket ID. */
    if(__atomic_load_n(&s->close_failed,__ATOMIC_ACQUIRE))return MTP_IO;
    if(s->probe>=0){rc=s->net.close(s->probe);s->probe=-1;if(rc){__atomic_store_n(&s->close_failed,1,__ATOMIC_RELEASE);return MTP_IO;}}
    for(i=0;i<PHONE_CLIENTS;++i)if(s->slots[i].fd>=0){rc=s->net.close(s->slots[i].fd);s->slots[i].fd=-1;if(rc){__atomic_store_n(&s->close_failed,1,__ATOMIC_RELEASE);return MTP_IO;}}
    if(s->hooks){mtp_client *c=s->source.client;c->control_user=s->old_control;c->io_checkpoint=s->old_checkpoint;c->paused_ms=s->old_paused;c->background_io=s->old_quiet;s->hooks=0;}
    if(s->listener>=0){rc=s->net.close(s->listener);s->listener=-1;if(rc){__atomic_store_n(&s->close_failed,1,__ATOMIC_RELEASE);return MTP_IO;}}
    if(s->pool>=0){s->net.pool_destroy(s->pool);s->pool=-1;}if(s->net_owned){s->net.term();s->net_owned=0;}
    if(s->module_loaded){
#ifdef PSMTP_TEST_PHONE_SERVER
        rc=phone_test_module_unload();
#else
        rc=sceSysmoduleUnloadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET);
#endif
        if(rc)return MTP_IO;s->module_loaded=0;
    }
    psmtp_phone_server_log(s,log);
    if(log)fprintf(log,"stage=phone-direct-io chunk=%zu read-calls=%" PRIu64 " read-bytes=%" PRIu64 " read-ms=%" PRIu64 " io-wait-ms=%" PRIu64 " send-ms=%" PRIu64 "\n",s->chunk_bytes,s->read_calls,s->read_bytes,s->read_ms,s->io_wait_ms,s->send_ms);
    if(log)fprintf(log,"stage=phone-direct-pipeline slots-per-client=%u buffer-bytes=%zu high-water=%u,%u,%u,%u reader-joined=%d\n",s->queue_slots,(size_t)PHONE_CLIENTS*s->queue_slots*s->chunk_bytes,s->slots[0].high_water,s->slots[1].high_water,s->slots[2].high_water,s->slots[3].high_water,s->reader_joined);
    if(log)fprintf(log,"stage=phone-direct-server closed=1 requests=%u source-error=%d staged-bytes=0 connections=%u http-body-bytes=%" PRIu64 " trace-dropped=%u\n",psmtp_phone_server_requests(s),psmtp_phone_server_error(s),s->connections,s->sent_bytes,s->trace_dropped);
    for(i=0;i<PHONE_CLIENTS;++i)buffer_free(s->slots[i].buffers);psmtp_sync_destroy(&s->state);psmtp_sync_destroy(&s->io);free(s);*owner=NULL;return 0;
}
int psmtp_phone_server_open(psmtp_phone_server **out,const psmtp_phone_source *source,char *url,size_t cap,FILE *log){static unsigned sequence;psmtp_phone_server *s;uint8_t addr[16];OrbisNetSocklen_t size=16;unsigned i,port;int rc,pause;const char *stage="allocate";
    if(!out || *out || !source || !source->client || !source->read || !source->control || (source->chunk_bytes && source->chunk_bytes!=PHONE_CHUNK && source->chunk_bytes!=MTP_PARTIAL_BASE_BYTES && source->chunk_bytes!=PHONE_MAX_CHUNK) || source->client->control_user || source->client->io_checkpoint || source->client->paused_ms || source->client->io_owner)return MTP_STATE;
    if(!phone_now())return MTP_UNSUPPORTED;
    s=calloc(1,sizeof(*s));if(!s)return MTP_MEMORY;*out=s;s->source=*source;s->listener=s->probe=s->pool=-1;s->chunk_bytes=source->chunk_bytes?source->chunk_bytes:PHONE_CHUNK;
    for(i=0;i<PHONE_CLIENTS;++i){s->slots[i].server=s;s->slots[i].index=i;s->slots[i].fd=-1;}
    rc=psmtp_sync_init(&s->io);if(rc){rc=MTP_IO;goto fail;}rc=psmtp_sync_init(&s->state);if(rc){rc=MTP_IO;goto fail;}
    rc=source->control(source->control_user,&pause,&s->epoch);if(rc)goto fail;
    stage="pipeline-buffers";s->queue_slots=s->chunk_bytes==PHONE_MAX_CHUNK?4:s->chunk_bytes==MTP_PARTIAL_BASE_BYTES?2:1;
    for(;;){int allocated=1;for(i=0;i<PHONE_CLIENTS;++i){s->slots[i].buffers=buffer_alloc(s->queue_slots*s->chunk_bytes);if(!s->slots[i].buffers){allocated=0;break;}}if(allocated)break;
        for(i=0;i<PHONE_CLIENTS;++i){buffer_free(s->slots[i].buffers);s->slots[i].buffers=NULL;}
        if(s->chunk_bytes==PHONE_CHUNK){rc=MTP_MEMORY;goto fail;}s->chunk_bytes=s->chunk_bytes==PHONE_MAX_CHUNK?MTP_PARTIAL_BASE_BYTES:PHONE_CHUNK;s->queue_slots=s->chunk_bytes==MTP_PARTIAL_BASE_BYTES?2:1;}
    stage="network-api";rc=net_load(s);if(rc)goto fail;stage="network-init";if(s->net.init()){rc=MTP_IO;goto fail;}s->net_owned=1;
    stage="network-pool";s->pool=s->net.pool_create("psmtp-phone",1048576,0);if(s->pool<0){rc=MTP_MEMORY;goto fail;}
    stage="listener";s->listener=s->net.socket("psmtp-phone",2,1,0);if(s->listener<0){rc=MTP_IO;goto fail;}rc=nonblock(s,s->listener);if(rc)goto fail;
    stage="loopback-bind";address(addr,0);if(s->net.bind(s->listener,(const OrbisNetSockaddr *)addr,16) || s->net.listen(s->listener,8) || s->net.name(s->listener,(OrbisNetSockaddr *)addr,&size) || size!=16 || addr[1]!=2 || addr[4]!=127 || addr[5] || addr[6] || addr[7]!=1){rc=MTP_IO;goto fail;}
    port=(unsigned)addr[2]*256u+addr[3];if(!port){rc=MTP_IO;goto fail;}
    snprintf(s->path,sizeof(s->path),"/phone-%" PRIx64 "-%08x.pkg",phone_now(),__atomic_add_fetch(&sequence,1,__ATOMIC_RELAXED));
    rc=snprintf(url,cap,"http://127.0.0.1:%u%s",port,s->path);if(rc<0 || (size_t)rc>=cap){rc=MTP_LIMIT;goto fail;}
    s->old_control=source->client->control_user;s->old_checkpoint=source->client->io_checkpoint;s->old_paused=source->client->paused_ms;s->old_quiet=source->client->background_io;
    source->client->control_user=s;source->client->io_checkpoint=phone_checkpoint;source->client->paused_ms=phone_paused;source->client->background_io=1;s->hooks=1;
    stage="mtp-reader";rc=scePthreadCreate(&s->reader,NULL,phone_reader,s,"psmtp-phone-read");if(rc){rc=MTP_IO;goto fail;}s->reader_started=1;
    stage="http-workers";for(i=0;i<PHONE_CLIENTS;++i){rc=scePthreadCreate(&s->slots[i].thread,NULL,phone_worker,&s->slots[i],"psmtp-phone");if(rc){rc=MTP_IO;goto fail;}s->slots[i].started=1;}
    stage="loopback-health";rc=health(s,addr);if(rc)goto fail;
    if(log)fprintf(log,"stage=phone-direct-server ready=1 loopback-only=1 workers=%u chunk=%zu staged-bytes=0 health=passed url=%s pipeline-slots=%u source-readers=1\n",PHONE_CLIENTS,s->chunk_bytes,url,s->queue_slots);return 0;
fail:
    if(log)fprintf(log,"stage=phone-direct-server ready=0 failed-at=%s app-status=%d staged-bytes=0\n",stage,rc);
    (void)psmtp_phone_server_close(out,1,log);return rc;
}
