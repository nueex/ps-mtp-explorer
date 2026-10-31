/* PS MTP Explorer - Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * See LICENSE and LICENSING.md. WITHOUT ANY WARRANTY. */
#include "download.h"
#include "lifecycle.h"
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#ifdef PSMTP_TEST_DOWNLOAD_PIPELINE
#include "ps4_download_test_api.h"
#else
#include <orbis/libkernel.h>
#endif
#include "native_sync.h"
static int queue_wait(psmtp_sync *s,unsigned *head,unsigned *tail,unsigned slots,int full,int *stop,int *eof){
    int rc=psmtp_sync_lock(s);unsigned h,t;if(rc)return MTP_IO;
    h=__atomic_load_n(head,__ATOMIC_ACQUIRE);t=__atomic_load_n(tail,__ATOMIC_ACQUIRE);
    if(!__atomic_load_n(stop,__ATOMIC_ACQUIRE) && (full?h-t>=slots:h==t && !__atomic_load_n(eof,__ATOMIC_ACQUIRE)))rc=psmtp_sync_wait(s,50000);
    psmtp_sync_unlock(s);return rc?MTP_IO:0;
}
static int pipeline_join(OrbisPthread thread){
#ifdef PSMTP_TEST_DOWNLOAD_PIPELINE
    return scePthreadJoin(thread,NULL);
#else
    return psmtp_lifecycle_join((void *)thread,NULL,250);
#endif
}
/* One USB owner, one ordered consumer, at most two queued blocks. The caller
 * cannot unwind its borrowed context until the consumer is joined. */
typedef struct {
    uint8_t *data;size_t capacity,length[2];unsigned head,tail;
    int stop,eof,error,error_number,done;OrbisPthread thread;psmtp_sync changed;
    int32_t priority,priority_status,priority_apply_status;
    mtp_download_consume consume;mtp_download_progress progress;void *user;mtp_client *client;
    uint64_t processed,total;unsigned high_water;
} download_pipe;
static int pipe_error(download_pipe *p){return __atomic_load_n(&p->error,__ATOMIC_ACQUIRE);}
static void *pipe_worker(void *v){
    download_pipe *p=v;int32_t current;
    if(p->client->io_owner)p->client->io_owner(p->client->control_user,2);
    if(!p->priority_status && !scePthreadGetprio(scePthreadSelf(),&current) && current!=p->priority)
        p->priority_apply_status=scePthreadSetprio(scePthreadSelf(),p->priority);
    for(;;){unsigned tail=__atomic_load_n(&p->tail,__ATOMIC_RELAXED),head=__atomic_load_n(&p->head,__ATOMIC_ACQUIRE);int rc;
        if(__atomic_load_n(&p->stop,__ATOMIC_ACQUIRE) || psmtp_lifecycle_requested())break;
        if(tail==head){
            /* EOF can become visible after the head snapshot above. Reload
             * head after acquiring EOF before deciding the queue is empty. */
            if(__atomic_load_n(&p->eof,__ATOMIC_ACQUIRE) && tail==__atomic_load_n(&p->head,__ATOMIC_ACQUIRE))break;
            if(p->progress && p->progress(p->user,p->processed,p->total)){__atomic_store_n(&p->error,MTP_CANCELLED,__ATOMIC_RELEASE);break;}
            if(queue_wait(&p->changed,&p->head,&p->tail,2,0,&p->stop,&p->eof)){__atomic_store_n(&p->error,MTP_IO,__ATOMIC_RELEASE);break;}continue;}
        if(p->progress && p->progress(p->user,p->processed,p->total)){__atomic_store_n(&p->error,MTP_CANCELLED,__ATOMIC_RELEASE);break;}
        rc=p->consume(p->user,p->data+(tail%2)*p->capacity,p->length[tail%2]);
        if(rc){p->error_number=errno;__atomic_store_n(&p->error,rc,__ATOMIC_RELEASE);break;}
        p->processed+=p->length[tail%2];
        if(p->progress && p->progress(p->user,p->processed,p->total)){__atomic_store_n(&p->error,MTP_CANCELLED,__ATOMIC_RELEASE);break;}
        __atomic_store_n(&p->tail,tail+1,__ATOMIC_RELEASE);psmtp_sync_notify(&p->changed);
    }
    if(p->client->io_owner)p->client->io_owner(p->client->control_user,0);
    __atomic_store_n(&p->done,1,__ATOMIC_RELEASE);psmtp_sync_notify(&p->changed);return NULL;
}
static int pipe_enqueue(void *v,const uint8_t *bytes,size_t size){
    download_pipe *p=v;unsigned head=__atomic_load_n(&p->head,__ATOMIC_RELAXED),tail;
    if(!size || size>p->capacity)return MTP_PROTOCOL;
    for(;;){int error=pipe_error(p);if(error)return error;if(p->client && p->client->io_checkpoint){error=p->client->io_checkpoint(p->client->control_user);if(error)return error;}if(psmtp_lifecycle_requested())return MTP_CANCELLED;
        tail=__atomic_load_n(&p->tail,__ATOMIC_ACQUIRE);if(head-tail<2)break;if(queue_wait(&p->changed,&p->head,&p->tail,2,1,&p->stop,&p->eof))return MTP_IO;}
    memcpy(p->data+(head%2)*p->capacity,bytes,size);p->length[head%2]=size;
    if(head-tail+1>p->high_water)p->high_water=head-tail+1;
    __atomic_store_n(&p->head,head+1,__ATOMIC_RELEASE);psmtp_sync_notify(&p->changed);return 0;
}
static int pipe_progress(void *v,uint64_t done,uint64_t total){
    download_pipe *p=v;(void)done;(void)total;
    /* Only the consumer accesses the original mutable copy context. */
    return pipe_error(p)!=0 || psmtp_lifecycle_requested();
}
static int stream_compatible(mtp_client *client,uint32_t id,uint64_t size,uint8_t *buffer,size_t capacity,mtp_download_consume consume,mtp_download_progress progress,void *user,mtp_download_stats *stats,FILE *log){
    download_pipe *p;int rc,wire_rc,joined,error,saved_errno,join_reported=0;
    if(!client || !consume || !buffer || capacity<MTP_RX_BYTES || capacity>1024u*1024u || capacity%1024)return MTP_STATE;
    if(size<2*capacity)return mtp_get_object_stream(client,id,size,buffer,capacity,consume,progress,user,stats);
    p=calloc(1,sizeof(*p));if(p)p->data=malloc(2*capacity);
    if(!p || !p->data){if(p)free(p);if(log)fputs("stage=stream-pipeline mode=serial reason=allocation-before-usb\n",log);return mtp_get_object_stream(client,id,size,buffer,capacity,consume,progress,user,stats);}
    p->capacity=capacity;p->consume=consume;p->progress=progress;p->user=user;p->total=size;p->client=client;
    p->priority_status=scePthreadGetprio(scePthreadSelf(),&p->priority);
    if(progress && progress(user,0,size)){free(p->data);free(p);return MTP_CANCELLED;}
    /* Log outside the consumer's lifetime: the work log has one producer. */
    if(log)fprintf(log,"stage=stream-pipeline mode=overlap queue-blocks=2 block-bytes=%zu extra-bytes=%zu\n",capacity,2*capacity);
    if(psmtp_sync_init(&p->changed)){free(p->data);free(p);return mtp_get_object_stream(client,id,size,buffer,capacity,consume,progress,user,stats);}
    if(client->io_owner)client->io_owner(client->control_user,1);
    rc=scePthreadCreate(&p->thread,NULL,pipe_worker,p,"psmtp-stream-sink");
    if(rc){if(client->io_owner)client->io_owner(client->control_user,0);psmtp_sync_destroy(&p->changed);free(p->data);free(p);if(log)fprintf(log,"stage=stream-pipeline mode=serial reason=create-before-usb native=%d\n",rc);return mtp_get_object_stream(client,id,size,buffer,capacity,consume,progress,user,stats);}
    rc=mtp_get_object_stream(client,id,size,buffer,capacity,pipe_enqueue,pipe_progress,p,stats);
    wire_rc=rc;
    if(rc)__atomic_store_n(&p->stop,1,__ATOMIC_RELEASE);
    __atomic_store_n(&p->eof,1,__ATOMIC_RELEASE);psmtp_sync_notify(&p->changed);
    if(client->io_checkpoint){int checkpoint=client->io_checkpoint(client->control_user);if(checkpoint && !rc)rc=checkpoint;}
    while((joined=pipeline_join(p->thread))!=0){
        /* Request UI-owned shutdown; never free a live borrowed context. */
        psmtp_lifecycle_request_exit();
        if(!join_reported && __atomic_load_n(&p->done,__ATOMIC_ACQUIRE)){
            if(log)fprintf(log,"stage=stream-pipeline-join-failed native=%d exit-requested=1 resources-retained=1\n",joined);join_reported=1;}
        (void)psmtp_sync_wait_event(&p->changed,50000);
    }
    if(client->io_owner)client->io_owner(client->control_user,0);
    error=pipe_error(p);saved_errno=errno;if(error){rc=error;if(error!=MTP_CANCELLED)saved_errno=p->error_number;}
    if(!rc && (psmtp_lifecycle_requested() || p->processed!=size))rc=psmtp_lifecycle_requested()?MTP_CANCELLED:MTP_VERIFY;
    /* Preserve a clean GetObject-not-supported response so copy_run can still
     * use its existing partial-read fallback. Never replay an active stream. */
    if(rc && !wire_rc)client->poisoned=1;
    if(log)fprintf(log,"stage=stream-pipeline-end status=%d consumed=%" PRIu64 " queue-high-water=%u joined=1 owner-priority=%d priority-query=%d priority-set=%d\n",rc,p->processed,p->high_water,p->priority,p->priority_status,p->priority_apply_status);
    psmtp_sync_destroy(&p->changed);free(p->data);free(p);errno=saved_errno;return rc;
}

/* T7 is isolated behind an explicit session setting. Off calls the T6
 * implementation above; selection is captured once before any USB command. */
#define PIPE_SLOTS 4u
#define PIPE_LARGE_BYTES (1024u*1024u)
#define PIPE_LARGE_THRESHOLD (8u*1024u*1024u)
/* Four owned slots are read directly and released after ordered consumption.
 * The caller cannot unwind its borrowed context until the consumer is joined. */
typedef struct {
    uint8_t *data,*slots[PIPE_SLOTS];size_t capacity,length[PIPE_SLOTS];unsigned head,tail;
    int stop,eof,error,error_number,done;OrbisPthread thread;psmtp_sync changed;
    int32_t priority,priority_status,priority_apply_status;
    mtp_download_consume consume;mtp_download_progress progress;void *user;
    mtp_client *client;
    uint64_t processed,total,direct_bytes,producer_wait_ms,consumer_wait_ms;unsigned high_water;
} boost_download_pipe;
static int boost_pipe_error(boost_download_pipe *p){return __atomic_load_n(&p->error,__ATOMIC_ACQUIRE);}
static uint64_t boost_pipe_now(boost_download_pipe *p){return p->client->io.now_ms(p->client->io.user);}
static void *boost_pipe_worker(void *v){
    boost_download_pipe *p=v;int32_t current;uint64_t waiting=0;
    if(p->client->io_owner)p->client->io_owner(p->client->control_user,2);
    if(!p->priority_status && !scePthreadGetprio(scePthreadSelf(),&current) && current!=p->priority)
        p->priority_apply_status=scePthreadSetprio(scePthreadSelf(),p->priority);
    for(;;){unsigned tail=__atomic_load_n(&p->tail,__ATOMIC_RELAXED),head=__atomic_load_n(&p->head,__ATOMIC_ACQUIRE);int rc;
        if(__atomic_load_n(&p->stop,__ATOMIC_ACQUIRE) || psmtp_lifecycle_requested())break;
        if(tail==head){
            /* EOF can become visible after the head snapshot above. Reload
             * head after acquiring EOF before deciding the queue is empty. */
            if(__atomic_load_n(&p->eof,__ATOMIC_ACQUIRE) && tail==__atomic_load_n(&p->head,__ATOMIC_ACQUIRE))break;
            if(!waiting)waiting=boost_pipe_now(p);
            if(p->progress && p->progress(p->user,p->processed,p->total)){__atomic_store_n(&p->error,MTP_CANCELLED,__ATOMIC_RELEASE);break;}
            if(queue_wait(&p->changed,&p->head,&p->tail,PIPE_SLOTS,0,&p->stop,&p->eof)){__atomic_store_n(&p->error,MTP_IO,__ATOMIC_RELEASE);break;}continue;}
        if(waiting){uint64_t now=boost_pipe_now(p);if(now>=waiting)p->consumer_wait_ms+=now-waiting;waiting=0;}
        if(p->progress && p->progress(p->user,p->processed,p->total)){__atomic_store_n(&p->error,MTP_CANCELLED,__ATOMIC_RELEASE);break;}
        rc=p->consume(p->user,p->slots[tail&(PIPE_SLOTS-1)],p->length[tail&(PIPE_SLOTS-1)]);
        if(rc){p->error_number=errno;__atomic_store_n(&p->error,rc,__ATOMIC_RELEASE);break;}
        p->processed+=p->length[tail&(PIPE_SLOTS-1)];
        if(p->progress && p->progress(p->user,p->processed,p->total)){__atomic_store_n(&p->error,MTP_CANCELLED,__ATOMIC_RELEASE);break;}
        __atomic_store_n(&p->tail,tail+1,__ATOMIC_RELEASE);psmtp_sync_notify(&p->changed);
    }
    if(p->client->io_owner)p->client->io_owner(p->client->control_user,0);
    __atomic_store_n(&p->done,1,__ATOMIC_RELEASE);psmtp_sync_notify(&p->changed);return NULL;
}
static int boost_pipe_acquire(void *v,uint8_t **bytes){
    boost_download_pipe *p=v;unsigned head=__atomic_load_n(&p->head,__ATOMIC_RELAXED),tail;uint64_t waiting=0;
    for(;;){int error=boost_pipe_error(p);if(error)return error;if(p->client && p->client->io_checkpoint){error=p->client->io_checkpoint(p->client->control_user);if(error)return error;}if(psmtp_lifecycle_requested())return MTP_CANCELLED;
        tail=__atomic_load_n(&p->tail,__ATOMIC_ACQUIRE);if(head-tail<PIPE_SLOTS)break;
        if(!waiting)waiting=boost_pipe_now(p);if(queue_wait(&p->changed,&p->head,&p->tail,PIPE_SLOTS,1,&p->stop,&p->eof))return MTP_IO;}
    if(waiting){uint64_t now=boost_pipe_now(p);if(now>=waiting)p->producer_wait_ms+=now-waiting;}
    *bytes=p->slots[head&(PIPE_SLOTS-1)];return 0;
}
static int boost_pipe_enqueue(void *v,const uint8_t *bytes,size_t size){
    boost_download_pipe *p=v;unsigned head=__atomic_load_n(&p->head,__ATOMIC_RELAXED),tail=__atomic_load_n(&p->tail,__ATOMIC_ACQUIRE);
    if(!size || size>p->capacity || head-tail>=PIPE_SLOTS || bytes!=p->slots[head&(PIPE_SLOTS-1)])return MTP_PROTOCOL;
    p->length[head&(PIPE_SLOTS-1)]=size;p->direct_bytes+=size;
    if(head-tail+1>p->high_water)p->high_water=head-tail+1;
    __atomic_store_n(&p->head,head+1,__ATOMIC_RELEASE);psmtp_sync_notify(&p->changed);return 0;
}
static int boost_pipe_progress(void *v,uint64_t done,uint64_t total){
    boost_download_pipe *p=v;(void)done;(void)total;
    /* Only the consumer accesses the original mutable copy context. */
    return boost_pipe_error(p)!=0 || psmtp_lifecycle_requested();
}
static int stream_boost(mtp_client *client,uint32_t id,uint64_t size,uint8_t *buffer,size_t capacity,mtp_download_consume consume,mtp_download_progress progress,void *user,mtp_download_stats *stats,FILE *log){
    boost_download_pipe *p;size_t target_capacity=capacity,extra;unsigned i;int rc,wire_rc,joined,error,saved_errno,join_reported=0;
    if(!client || !client->io.now_ms || !consume || !buffer || capacity<MTP_RX_BYTES || capacity>1024u*1024u || capacity%1024)return MTP_STATE;
    if(size<2*capacity)return mtp_get_object_stream(client,id,size,buffer,capacity,consume,progress,user,stats);
    if(size>=PIPE_LARGE_THRESHOLD && capacity<PIPE_LARGE_BYTES)target_capacity=PIPE_LARGE_BYTES;
    extra=(target_capacity==capacity?PIPE_SLOTS-1:PIPE_SLOTS)*target_capacity;
    p=calloc(1,sizeof(*p));if(p)p->data=malloc(extra);
    if(p && !p->data && target_capacity!=capacity){
        if(log)fputs("stage=stream-pipeline allocation-downscale-before-usb=1\n",log);
        target_capacity=capacity;extra=(PIPE_SLOTS-1)*capacity;p->data=malloc(extra);}
    if(!p || !p->data){if(p)free(p);if(log)fputs("stage=stream-pipeline mode=serial reason=allocation-before-usb\n",log);return mtp_get_object_stream(client,id,size,buffer,capacity,consume,progress,user,stats);}
    p->capacity=target_capacity;p->consume=consume;p->progress=progress;p->user=user;p->total=size;p->client=client;
    for(i=0;i<PIPE_SLOTS;++i)p->slots[i]=target_capacity==capacity?(i?p->data+(i-1)*target_capacity:buffer):p->data+i*target_capacity;
    p->priority_status=scePthreadGetprio(scePthreadSelf(),&p->priority);
    if(progress && progress(user,0,size)){free(p->data);free(p);return MTP_CANCELLED;}
    /* Log outside the consumer's lifetime: the work log has one producer. */
    if(log)fprintf(log,"stage=stream-pipeline mode=direct-overlap queue-blocks=%u block-bytes=%zu extra-bytes=%zu large-read-experiment=%d\n",PIPE_SLOTS,target_capacity,extra,1);
    if(psmtp_sync_init(&p->changed)){free(p->data);free(p);return mtp_get_object_stream(client,id,size,buffer,capacity,consume,progress,user,stats);}
    if(client->io_owner)client->io_owner(client->control_user,1);
    rc=scePthreadCreate(&p->thread,NULL,boost_pipe_worker,p,"psmtp-stream-sink");
    if(rc){if(client->io_owner)client->io_owner(client->control_user,0);psmtp_sync_destroy(&p->changed);free(p->data);free(p);if(log)fprintf(log,"stage=stream-pipeline mode=serial reason=create-before-usb native=%d\n",rc);return mtp_get_object_stream(client,id,size,buffer,capacity,consume,progress,user,stats);}
    rc=mtp_get_object_stream_buffers(client,id,size,p->slots[0],target_capacity,boost_pipe_enqueue,boost_pipe_progress,p,stats,boost_pipe_acquire);
    wire_rc=rc;
    if(rc)__atomic_store_n(&p->stop,1,__ATOMIC_RELEASE);
    __atomic_store_n(&p->eof,1,__ATOMIC_RELEASE);psmtp_sync_notify(&p->changed);
    if(client->io_checkpoint){int checkpoint=client->io_checkpoint(client->control_user);if(checkpoint && !rc)rc=checkpoint;}
    while((joined=pipeline_join(p->thread))!=0){
        /* Request UI-owned shutdown; never free a live borrowed context. */
        psmtp_lifecycle_request_exit();
        if(!join_reported && __atomic_load_n(&p->done,__ATOMIC_ACQUIRE)){
            if(log)fprintf(log,"stage=stream-pipeline-join-failed native=%d exit-requested=1 resources-retained=1\n",joined);join_reported=1;}
        (void)psmtp_sync_wait_event(&p->changed,50000);
    }
    if(client->io_owner)client->io_owner(client->control_user,0);
    error=boost_pipe_error(p);saved_errno=errno;if(error){rc=error;if(error!=MTP_CANCELLED)saved_errno=p->error_number;}
    if(!rc && (psmtp_lifecycle_requested() || p->processed!=size))rc=psmtp_lifecycle_requested()?MTP_CANCELLED:MTP_VERIFY;
    /* Preserve a clean GetObject-not-supported response so copy_run can still
     * use its existing partial-read fallback. Never replay an active stream. */
    if(rc && !wire_rc)client->poisoned=1;
    if(log)fprintf(log,"stage=stream-pipeline-end status=%d consumed=%" PRIu64 " direct-bytes=%" PRIu64 " payload-queue-copied=0 queue-high-water=%u block-bytes=%zu producer-wait-ms=%" PRIu64 " consumer-wait-ms=%" PRIu64 " joined=1 owner-priority=%d priority-query=%d priority-set=%d\n",rc,p->processed,p->direct_bytes,p->high_water,target_capacity,p->producer_wait_ms,p->consumer_wait_ms,p->priority,p->priority_status,p->priority_apply_status);
    psmtp_sync_destroy(&p->changed);free(p->data);free(p);errno=saved_errno;return rc;
}

static int stream_boost_enabled;
void psmtp_stream_boost_set(int enabled){__atomic_store_n(&stream_boost_enabled,!!enabled,__ATOMIC_RELEASE);}
int psmtp_stream_boost_get(void){return __atomic_load_n(&stream_boost_enabled,__ATOMIC_ACQUIRE);}
int psmtp_stream_download(mtp_client *client,uint32_t id,uint64_t size,uint8_t *buffer,size_t capacity,mtp_download_consume consume,mtp_download_progress progress,void *user,mtp_download_stats *stats,FILE *log){
    int boost=psmtp_stream_boost_get();
    if(log)fprintf(log,"stage=stream-profile speed-boost=%d base=t6 selected=%s\n",boost,boost?"t7":"t6");
    return boost?stream_boost(client,id,size,buffer,capacity,consume,progress,user,stats,log):stream_compatible(client,id,size,buffer,capacity,consume,progress,user,stats,log);
}
