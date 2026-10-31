/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE.
 * WITHOUT ANY WARRANTY; see LICENSE.
 */
#define _GNU_SOURCE
#include "work.h"
#include "lifecycle.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef PSMTP_TEST_WORK
#include "ps4_work_test_api.h"
#else
#include <orbis/libkernel.h>
#endif
#include "native_sync.h"
#include "work_io.h"
#ifndef PSMTP_TEST_WORK
#include "monotonic.h"
#endif
#define WORK_LOG_CAPACITY 262144u
struct psmtp_work {
    OrbisPthread thread;int started,done,cancel,result;
    psmtp_sync gate,mailbox,log_writer;OrbisPthread primary,io_thread;
    int pause_requested,shutdown_requested,paused,error,separate_owner;
    unsigned system_epoch,flush_epoch,flushed_epoch,resume_epoch,validated_epoch,transport_epoch;
    uint64_t pause_started,pause_total,last_checkpoint;
    int (*flush)(void *),(*validate)(void *);void *io_user;
    int32_t owner_priority,owner_priority_status;
    psmtp_work_fn fn;void *user;mtp_client *client;FILE *log;
    psmtp_work_progress_state progress;
    unsigned head,tail,dropped;char bytes[WORK_LOG_CAPACITY];
};
#include "psmtp/cleanup_log.h"

static __thread psmtp_work *current_work;
static uint64_t work_now(void){
#if defined(PSMTP_TEST_WORK) && defined(_WIN32)
    return GetTickCount64();
#elif defined(PSMTP_TEST_WORK)
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000;
#else
    struct timespec t={0};return psmtp_monotonic_time(&t)?0:(uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000;
#endif
}
static void work_error(psmtp_work *w,int rc){if(rc)__atomic_store_n(&w->error,rc,__ATOMIC_RELEASE);psmtp_sync_notify(&w->gate);}
int psmtp_work_pause_requested(psmtp_work *w){return w && __atomic_load_n(&w->pause_requested,__ATOMIC_ACQUIRE);}
void psmtp_work_system(psmtp_work *w,int paused,unsigned epoch,int shutdown){
    uint64_t now;int changed=0;if(!w)return;if(psmtp_sync_lock(&w->gate)){work_error(w,MTP_IO);return;}now=work_now();
    if(paused!=w->pause_requested){
        changed=1;
        if(paused){w->pause_started=now;++w->flush_epoch;}
        else {if(now>=w->pause_started)w->pause_total+=now-w->pause_started;w->pause_started=0;++w->resume_epoch;}
        __atomic_store_n(&w->pause_requested,!!paused,__ATOMIC_RELEASE);
    }
    if(epoch!=w->system_epoch){w->system_epoch=epoch;++w->resume_epoch;changed=1;}
    if(shutdown && !w->shutdown_requested){__atomic_store_n(&w->shutdown_requested,1,__ATOMIC_RELEASE);changed=1;}
    if(changed)psmtp_sync_signal_locked(&w->gate);psmtp_sync_unlock(&w->gate);
}
static uint64_t work_paused_ms(void *user){psmtp_work *w=user;uint64_t result,now=work_now();if(psmtp_sync_lock(&w->gate))return 0;
    result=w->pause_total;if(w->pause_requested && now>=w->pause_started)result+=now-w->pause_started;psmtp_sync_unlock(&w->gate);return result;
}
static void work_io_owner(void *user,int state){psmtp_work *w=user;if(psmtp_sync_lock(&w->gate)){work_error(w,MTP_IO);return;}
    if(state==1){w->separate_owner=1;memset(&w->io_thread,0,sizeof(w->io_thread));}
    else if(state==2)w->io_thread=scePthreadSelf();
    else {w->separate_owner=0;w->io_thread=w->primary;}
    psmtp_sync_signal_locked(&w->gate);psmtp_sync_unlock(&w->gate);
}
static int work_checkpoint(void *user){
    psmtp_work *w=user;int rc;
    for(;;){unsigned flush_epoch,resume_epoch;int do_flush,do_validate,do_transport,file_owner,transport_owner;int (*flush)(void *),(*validate)(void *);void *io_user;
        rc=__atomic_load_n(&w->error,__ATOMIC_ACQUIRE);if(rc)return rc;
        if(psmtp_work_cancelled(w))return MTP_CANCELLED;
        if(psmtp_sync_lock(&w->gate))return MTP_IO;
        {uint64_t now=work_now();if(!w->pause_requested && w->last_checkpoint && now>w->last_checkpoint && now-w->last_checkpoint>=2000){++w->flush_epoch;++w->resume_epoch;}w->last_checkpoint=now;}
        do_flush=w->flushed_epoch!=w->flush_epoch;do_validate=!w->pause_requested && w->validated_epoch!=w->resume_epoch;
        do_transport=!w->pause_requested && w->client && w->transport_epoch!=w->resume_epoch;
        if(!do_flush && !do_validate && !do_transport && !w->pause_requested){__atomic_store_n(&w->paused,0,__ATOMIC_RELEASE);psmtp_sync_unlock(&w->gate);return 0;}
        /* The producer may never fsync underneath a pipeline consumer write.
         * Handoff is published before the consumer starts; only its owner
         * services file hooks, including while the queue is empty. */
        file_owner=scePthreadSelf()==w->io_thread && (do_flush || do_validate);
        transport_owner=scePthreadSelf()==w->primary && !do_flush && !do_validate && do_transport;
        if(!file_owner && !transport_owner){
            if(scePthreadSelf()==w->io_thread)__atomic_store_n(&w->paused,1,__ATOMIC_RELEASE);
            rc=psmtp_sync_wait(&w->gate,50000);psmtp_sync_unlock(&w->gate);if(rc){work_error(w,MTP_IO);return MTP_IO;}continue;
        }
        flush_epoch=w->flush_epoch;resume_epoch=w->resume_epoch;flush=w->flush;validate=w->validate;io_user=w->io_user;
        psmtp_sync_unlock(&w->gate);
        if(file_owner && do_flush){rc=flush?flush(io_user):0;if(rc){work_error(w,rc);return rc;}
            fprintf(w->log,"stage=transfer-pause-flushed epoch=%u status=0\n",flush_epoch);}
        if(file_owner && do_validate){
            rc=validate?validate(io_user):0;
            fprintf(w->log,"stage=transfer-resume-check epoch=%u status=%d reenumeration-replay=0\n",resume_epoch,rc);
            if(rc){work_error(w,rc);return rc;}
        }
        if(transport_owner){rc=w->client->resume_check?w->client->resume_check(w->client->io.user):MTP_UNSUPPORTED;
            fprintf(w->log,"stage=transfer-usb-resume-check epoch=%u status=%d\n",resume_epoch,rc);if(rc){work_error(w,rc);return rc;}}
        if(psmtp_sync_lock(&w->gate))return MTP_IO;
        if(file_owner && do_flush)w->flushed_epoch=flush_epoch;if(file_owner && do_validate)w->validated_epoch=resume_epoch;if(transport_owner)w->transport_epoch=resume_epoch;
        psmtp_sync_signal_locked(&w->gate);psmtp_sync_unlock(&w->gate);
    }
}
void psmtp_work_bind_io(psmtp_work_io_scope *scope,int (*flush)(void *),int (*validate)(void *),void *user){
    psmtp_work *w=current_work;memset(scope,0,sizeof(*scope));if(!w)return;
    if(psmtp_sync_lock(&w->gate)){work_error(w,MTP_IO);return;}
    scope->work=w;scope->flush=w->flush;scope->validate=w->validate;scope->user=w->io_user;
    w->flush=flush;w->validate=validate;w->io_user=user;psmtp_sync_unlock(&w->gate);
}
void psmtp_work_unbind_io(psmtp_work_io_scope *scope){psmtp_work *w=scope->work;if(!w)return;
    if(psmtp_sync_lock(&w->gate)){work_error(w,MTP_IO);return;}
    w->flush=scope->flush;w->validate=scope->validate;w->io_user=scope->user;scope->work=NULL;psmtp_sync_unlock(&w->gate);
}
static ssize_t work_log_write(void *v,const char *bytes,size_t n){
    psmtp_work *w=v;unsigned head,tail;
    if(psmtp_sync_trylock(&w->log_writer)){__atomic_fetch_add(&w->dropped,1,__ATOMIC_RELAXED);return (ssize_t)n;}
    head=__atomic_load_n(&w->head,__ATOMIC_RELAXED);tail=__atomic_load_n(&w->tail,__ATOMIC_ACQUIRE);
    size_t available=WORK_LOG_CAPACITY-(unsigned)(head-tail),first;
    /* Diagnostics cannot block file I/O or grow without bound. The UI records
     * any overflow explicitly. USB packet traces are quiet while borrowed. */
    if(n>available){__atomic_fetch_add(&w->dropped,1,__ATOMIC_RELAXED);psmtp_sync_unlock(&w->log_writer);return (ssize_t)n;}
    first=WORK_LOG_CAPACITY-head%WORK_LOG_CAPACITY;if(first>n)first=n;
    memcpy(w->bytes+head%WORK_LOG_CAPACITY,bytes,first);memcpy(w->bytes,bytes+first,n-first);
    __atomic_store_n(&w->head,head+(unsigned)n,__ATOMIC_RELEASE);psmtp_sync_unlock(&w->log_writer);return (ssize_t)n;
}
psmtp_work *psmtp_work_create(void){
    psmtp_work *w=calloc(1,sizeof(*w));if(!w)return NULL;
    if(psmtp_sync_init(&w->gate)){free(w);return NULL;}
    if(psmtp_sync_init(&w->mailbox)){psmtp_sync_destroy(&w->gate);free(w);return NULL;}
    if(psmtp_sync_init(&w->log_writer)){psmtp_sync_destroy(&w->mailbox);psmtp_sync_destroy(&w->gate);free(w);return NULL;}
#if !defined(PSMTP_PRODUCTION) && !defined(PSMTP_TEST_WORK)
    cookie_io_functions_t io={0};io.write=work_log_write;w->log=fopencookie(w,"w",io);
    if(!w->log){psmtp_sync_destroy(&w->log_writer);psmtp_sync_destroy(&w->mailbox);psmtp_sync_destroy(&w->gate);free(w);return NULL;}setvbuf(w->log,NULL,_IONBF,0);
#else
    w->log=stdout;
    (void)work_log_write;
#endif
    return w;
}
FILE *psmtp_work_log(psmtp_work *w){return w->log;}
int psmtp_work_cancelled(psmtp_work *w){return __atomic_load_n(&w->cancel,__ATOMIC_ACQUIRE) || __atomic_load_n(&w->shutdown_requested,__ATOMIC_ACQUIRE) || __atomic_load_n(&w->error,__ATOMIC_ACQUIRE) || psmtp_lifecycle_requested();}
int psmtp_work_source_state(void *user,int *pause,unsigned *epoch){psmtp_work *w=user;int cancelled=psmtp_work_cancelled(w);if(psmtp_sync_lock(&w->gate))return MTP_IO;*pause=w->pause_requested;*epoch=w->resume_epoch;psmtp_sync_unlock(&w->gate);return cancelled?MTP_CANCELLED:0;}
void psmtp_work_cancel(psmtp_work *w){if(w){__atomic_store_n(&w->cancel,1,__ATOMIC_RELEASE);psmtp_sync_notify(&w->gate);}}
void psmtp_work_shutdown(psmtp_work *w){if(w){__atomic_store_n(&w->shutdown_requested,1,__ATOMIC_RELEASE);__atomic_store_n(&w->cancel,1,__ATOMIC_RELEASE);psmtp_sync_wake(&w->gate);}}
int psmtp_work_progress(void *v,enum mtp_copy_phase phase,uint64_t done,uint64_t total){
    psmtp_work *w=v;
    if(w->started && work_checkpoint(w))return 1;
    if(psmtp_sync_lock(&w->mailbox)){work_error(w,MTP_IO);return 1;}
    w->progress.phase=phase;w->progress.done=done;w->progress.total=total;w->progress.install=0;
    psmtp_sync_unlock(&w->mailbox);return psmtp_work_cancelled(w);
}
void psmtp_work_install_progress(psmtp_work *w,const psmtp_install_report *r){
    if(w->started && work_checkpoint(w))return;
    if(psmtp_sync_lock(&w->mailbox)){work_error(w,MTP_IO);return;}
    w->progress.install=1;w->progress.phase=MTP_COPY_RECEIVE;w->progress.done=r->done;w->progress.total=r->total;w->progress.report=*r;
    psmtp_sync_unlock(&w->mailbox);
}
int psmtp_work_snapshot(psmtp_work *w,psmtp_work_progress_state *out){
    /* Main never waits for the producer, and never holds a mailbox lock while
     * rendering, logging, polling input or entering a system service. */
    if(psmtp_sync_trylock(&w->mailbox))return 0;
    *out=w->progress;psmtp_sync_unlock(&w->mailbox);
    out->cancel_requested=__atomic_load_n(&w->cancel,__ATOMIC_ACQUIRE);out->pause_requested=psmtp_work_pause_requested(w);
    out->shutdown_requested=__atomic_load_n(&w->shutdown_requested,__ATOMIC_ACQUIRE);out->paused=__atomic_load_n(&w->paused,__ATOMIC_ACQUIRE);out->error=__atomic_load_n(&w->error,__ATOMIC_ACQUIRE);return 1;
}
static void *work_entry(void *v){
    psmtp_work *w=v;int quiet=0;int32_t before=0,after=0,query,set=0;OrbisPthread self=scePthreadSelf();
    query=scePthreadGetprio(self,&before);
    /* Match the caller's measured priority; never choose an arbitrary higher
     * priority or pin a core. Keep native failures visible and nonfatal. */
    if(!w->owner_priority_status && !query && before!=w->owner_priority)set=scePthreadSetprio(self,w->owner_priority);
    {int32_t after_status=scePthreadGetprio(self,&after);
    fprintf(w->log,"stage=operation-thread-priority owner=%d owner-status=%d before=%d query-status=%d set-status=%d after=%d after-status=%d\n",
        w->owner_priority,w->owner_priority_status,before,query,set,after,after_status);}
    if(w->client){quiet=w->client->background_io;w->client->background_native_error=0;w->client->background_io=1;}
    w->primary=w->io_thread=self;current_work=w;
    if(w->client){void *old_user=w->client->control_user;int (*old_checkpoint)(void *)=w->client->io_checkpoint;uint64_t (*old_clock)(void *)=w->client->paused_ms;void (*old_owner)(void *,int)=w->client->io_owner;
        w->client->control_user=w;w->client->io_checkpoint=work_checkpoint;w->client->paused_ms=work_paused_ms;w->client->io_owner=work_io_owner;
        w->result=w->fn(w->user,w);
        w->client->control_user=old_user;w->client->io_checkpoint=old_checkpoint;w->client->paused_ms=old_clock;w->client->io_owner=old_owner;
    }else w->result=w->fn(w->user,w);
    current_work=NULL;if(__atomic_load_n(&w->error,__ATOMIC_ACQUIRE))w->result=__atomic_load_n(&w->error,__ATOMIC_ACQUIRE);
    if(w->client){int32_t native=w->client->background_native_error;w->client->background_io=quiet;
        if(native)fprintf(w->log,"stage=operation-usb-native-error code=0x%08x status=%d\n",(unsigned)native,w->result);}
    __atomic_store_n(&w->done,1,__ATOMIC_RELEASE);psmtp_sync_notify(&w->gate);return NULL;
}
int psmtp_work_start(psmtp_work *w,psmtp_work_fn fn,void *user,mtp_client *client){
    int rc;if(w->started || !fn)return MTP_STATE;if(__atomic_load_n(&w->shutdown_requested,__ATOMIC_ACQUIRE))return MTP_CANCELLED;
    memset(&w->progress,0,sizeof(w->progress));w->progress.phase=MTP_COPY_PREPARE;
    w->fn=fn;w->user=user;w->client=client;w->result=0;
    w->flush=w->validate=NULL;w->io_user=NULL;w->separate_owner=0;
    w->last_checkpoint=0;
    w->flushed_epoch=w->flush_epoch;w->validated_epoch=w->transport_epoch=w->resume_epoch;
    if(psmtp_work_pause_requested(w))++w->flush_epoch;
    __atomic_store_n(&w->paused,0,__ATOMIC_RELEASE);__atomic_store_n(&w->error,0,__ATOMIC_RELEASE);
    w->owner_priority_status=scePthreadGetprio(scePthreadSelf(),&w->owner_priority);
    __atomic_store_n(&w->done,0,__ATOMIC_RELEASE);__atomic_store_n(&w->cancel,0,__ATOMIC_RELEASE);
    w->started=1;rc=scePthreadCreate(&w->thread,NULL,work_entry,w,"psmtp-operation");
    if(rc){w->started=0;fprintf(w->log,"stage=operation-worker-create native=0x%08x\n",(unsigned)rc);return MTP_IO;}
    return 0;
}
int psmtp_work_done(psmtp_work *w,int *result){
    if(!__atomic_load_n(&w->done,__ATOMIC_ACQUIRE))return 0;
    *result=w->result;return 1;
}
size_t psmtp_work_read_log(psmtp_work *w,char *out,size_t cap){
    unsigned tail=__atomic_load_n(&w->tail,__ATOMIC_RELAXED),head=__atomic_load_n(&w->head,__ATOMIC_ACQUIRE);
    size_t n=(unsigned)(head-tail),first=WORK_LOG_CAPACITY-tail%WORK_LOG_CAPACITY;if(n>cap)n=cap;if(first>n)first=n;
    memcpy(out,w->bytes+tail%WORK_LOG_CAPACITY,first);memcpy(out+first,w->bytes,n-first);
    __atomic_store_n(&w->tail,tail+(unsigned)n,__ATOMIC_RELEASE);return n;
}
unsigned psmtp_work_dropped_logs(psmtp_work *w){return __atomic_exchange_n(&w->dropped,0,__ATOMIC_ACQ_REL);}
int psmtp_work_join(psmtp_work *w,unsigned timeout_ms){
    uint64_t end;int rc;if(!w || !w->started)return 0;end=work_now()+timeout_ms;
    while(!__atomic_load_n(&w->done,__ATOMIC_ACQUIRE) && work_now()<end){if(psmtp_work_wait(w,10))return MTP_IO;}
    if(!__atomic_load_n(&w->done,__ATOMIC_ACQUIRE))return MTP_TIMEOUT;
#ifdef PSMTP_TEST_WORK
    rc=scePthreadJoin(w->thread,NULL);
#else
    rc=psmtp_lifecycle_join((void *)w->thread,NULL,1000);
#endif
    if(rc)return rc;w->started=0;w->user=NULL;w->client=NULL;return 0;
}
int psmtp_work_wait(psmtp_work *w,unsigned ms){if(!w)return MTP_STATE;if(ms>50)ms=50;return psmtp_sync_wait_event(&w->gate,ms*1000)?MTP_IO:0;}
int psmtp_work_destroy(psmtp_work *w){
    if(!w)return 0;if(w->started)return MTP_STATE;
    if(w->log && w->log!=stdout)fclose(w->log);psmtp_sync_destroy(&w->log_writer);psmtp_sync_destroy(&w->mailbox);psmtp_sync_destroy(&w->gate);free(w);return 0;
}
