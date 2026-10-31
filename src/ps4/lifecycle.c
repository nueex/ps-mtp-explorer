/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
/* Native lifecycle ABI evidence and limitations: docs/SYSTEM_CLOSE_INVESTIGATION.md.
 * No cleanup executes in a signal handler. A dedicated append/sync journal
 * survives UI suspension and records the last completed cleanup boundary. */
#include "lifecycle.h"
#include "psmtp/log_paths.h"
#include "psmtp/version.h"
#ifndef PSMTP_DIAGNOSTIC_BUILD
#define PSMTP_DIAGNOSTIC_BUILD "20261028-lifecycle-" PSMTP_VERSION
#endif
#ifdef PSMTP_TEST_NATIVE_LIFECYCLE
#include "ps4_lifecycle_test_api.h"
#else
#include <orbis/libkernel.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#endif
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "monotonic.h"
#include <inttypes.h>
#include "psmtp/shutdown.h"

typedef void (*native_handler)(int);
typedef struct {native_handler handler;int32_t flags;uint32_t mask[4];} native_action;
typedef char native_action_layout[(sizeof(native_action)==32 && offsetof(native_action,flags)==8 && offsetof(native_action,mask)==12)?1:-1];
/* Native sigaction uses Orbis/FreeBSD flags, NOT the SDK's Linux SA_* values. */
static struct {
    int fd,requested,installed,last_overlay,last_background,status_error,closing;
    unsigned shutdown_count;
    struct {char stage[80];int status,ready;} shutdown_records[128];
    int pause;unsigned resume_epoch;
    uint64_t last_poll,last_heartbeat;
    int (*action)(int,const native_action *,native_action *);
    int (*mask)(int,const uint32_t *,uint32_t *);
    int32_t (*status)(void *),(*event)(void *);
    int32_t (*timedjoin)(void *,void **,const struct timespec *);
} life={.fd=-1,.last_overlay=-1,.last_background=-1};

static uint64_t lifecycle_ms(void){struct timespec ts={0};if(psmtp_monotonic_time(&ts) || (!ts.tv_sec && !ts.tv_nsec)){if(clock_gettime(CLOCK_REALTIME,&ts))return 0;}return (uint64_t)ts.tv_sec*1000+(uint64_t)ts.tv_nsec/1000000;}
#ifndef PSMTP_PRODUCTION
static void journal_write(const char *bytes,size_t length){
    int fd=__atomic_load_n(&life.fd,__ATOMIC_ACQUIRE);size_t done=0;
    if(fd<0)return;
    while(done<length){ssize_t n=write(fd,bytes+done,length-done);if(n<0 && errno==EINTR)continue;if(n<=0)return;done+=(size_t)n;}
}
#endif
static void close_signal(int signo){
    int saved_errno=errno;
    /* Intent only: even an async-signal-safe O_SYNC write can block. */
    __atomic_store_n(&life.requested,signo,__ATOMIC_RELEASE);
    errno=saved_errno;
}
int psmtp_lifecycle_requested(void){return __atomic_load_n(&life.requested,__ATOMIC_ACQUIRE);}
void psmtp_lifecycle_request_exit(void){__atomic_store_n(&life.requested,1,__ATOMIC_RELEASE);}
void psmtp_lifecycle_shutdown_begin(void){
    int expected=0;
    (void)__atomic_compare_exchange_n(&life.requested,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
    psmtp_shutdown_quiet_begin();__atomic_store_n(&life.closing,1,__ATOMIC_RELEASE);
}
void psmtp_lifecycle_mark(const char *stage,int status){
#ifdef PSMTP_PRODUCTION
    (void)stage;(void)status;
#else
    if(__atomic_load_n(&life.closing,__ATOMIC_ACQUIRE)){
        unsigned i=__atomic_fetch_add(&life.shutdown_count,1,__ATOMIC_RELAXED);size_t n;
        if(i>=128)return;n=strnlen(stage,79);memcpy(life.shutdown_records[i].stage,stage,n);life.shutdown_records[i].stage[n]=0;
        life.shutdown_records[i].status=status;__atomic_store_n(&life.shutdown_records[i].ready,1,__ATOMIC_RELEASE);return;
    }
    int saved_errno=errno;char line[256];int n=snprintf(line,sizeof(line),"pid=%d ms=%" PRIu64 " stage=%s status=%d\n",(int)getpid(),lifecycle_ms(),stage,status);
    if(n>0 && (size_t)n<sizeof(line))journal_write(line,(size_t)n);
    errno=saved_errno;
#endif
}
static int lifecycle_module(const char *name){
    OrbisKernelModule ids[128];size_t n=0,i;if(sceKernelGetModuleList(ids,128,&n) || n>128)return -1;
    for(i=0;i<n;++i){OrbisKernelModuleInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);if(sceKernelGetModuleInfo(ids[i],&info))continue;
        if(strstr(info.name,name))return (int)ids[i];}
    return -1;
}
static int lifecycle_symbol(int module,const char *name,void *out,size_t size){
    void *p=NULL;if(module<0 || size!=sizeof(p) || sceKernelDlsym(module,name,&p) || !p)return -1;memcpy(out,&p,size);return 0;
}
int psmtp_lifecycle_open(FILE *log){
    int module,rc=0;native_action action,readback;uint32_t set[4]={0};
#ifndef PSMTP_PRODUCTION
    if(life.fd<0){
        if(mkdir(PSMTP_LOG_DIRECTORY,0755) && errno!=EEXIST)rc=-1;
        else life.fd=open(PSMTP_SHUTDOWN_LOG_PATH,O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC|O_NOFOLLOW,0644);
        if(life.fd<0){if(log)fprintf(log,"stage=shutdown-journal unavailable errno=%d\n",errno);}
        else {
            psmtp_lifecycle_mark("run-start-build-" PSMTP_DIAGNOSTIC_BUILD,0);
            int mode_rc=fchmod(life.fd,0644);
            psmtp_lifecycle_mark("shutdown-journal-ftp-readable",mode_rc);
        }
    }
    if(life.installed)return life.fd<0?-1:0;
#else
    if(life.installed)return 0;
#endif
    module=lifecycle_module("libkernel");
    if(lifecycle_symbol(module,"sigaction",&life.action,sizeof(life.action)) || lifecycle_symbol(module,"sigprocmask",&life.mask,sizeof(life.mask))){psmtp_lifecycle_mark("native-signal-api-unavailable",module);return -1;}
    (void)lifecycle_symbol(module,"scePthreadTimedjoin",&life.timedjoin,sizeof(life.timedjoin));
    psmtp_lifecycle_mark("native-timedjoin-available",life.timedjoin?0:-1);
    memset(&action,0,sizeof(action));action.handler=close_signal;
    for(int sig=2;sig<=15;sig+=13){
        memset(&readback,0,sizeof(readback));
        rc=life.action(sig,&action,NULL);
        if(!rc)rc=life.action(sig,NULL,&readback);
        if(!rc && readback.handler!=close_signal)rc=-1;
        psmtp_lifecycle_mark(sig==15?"native-sigterm-install-verified":"native-sigint-install-verified",rc);
        if(rc)return -1;
        set[(sig-1)/32]|=UINT32_C(1)<<((sig-1)%32);
    }
    /* FreeBSD SIG_UNBLOCK=2, verified against native sigprocmask. The SDK's
     * SIG_UNBLOCK=1 is Linux and would BLOCK the close request instead. */
    rc=life.mask(2,set,NULL);psmtp_lifecycle_mark("native-close-signals-unblocked",rc);if(rc)return -1;
    {uint32_t read_mask[4]={0};rc=life.mask(2,NULL,read_mask);
    if(!rc && (read_mask[0]&set[0]))rc=-1;
    psmtp_lifecycle_mark("native-close-signal-mask-verified",rc);if(rc)return -1;}
    life.installed=1;
    module=lifecycle_module("libSceSystemService");
    (void)lifecycle_symbol(module,"sceSystemServiceGetStatus",&life.status,sizeof(life.status));
    (void)lifecycle_symbol(module,"sceSystemServiceReceiveEvent",&life.event,sizeof(life.event));
    psmtp_lifecycle_mark("system-status-observer",life.status?0:-1);
    if(log)fprintf(log,"stage=shutdown-monitor native-sigterm=verified journal=%s status-observer=%d\n",PSMTP_SHUTDOWN_LOG_PATH,life.status!=NULL);
#ifdef PSMTP_PRODUCTION
    return 0;
#else
    return life.fd<0?-1:0;
#endif
}
static void lifecycle_poll_impl(void){
    union {uint64_t align;unsigned char bytes[256];} status;
    union {uint64_t align;unsigned char bytes[8200];} event;
    uint64_t now=lifecycle_ms();int32_t rc,event_count=0;int overlay,background;
    if(psmtp_lifecycle_requested())return;
    if(life.last_poll && now-life.last_poll<100)return;
    /* A suspend may freeze every user thread before it can observe status.
     * Treat a missing UI heartbeat as a conservative resume/revalidation
     * boundary, without interpreting an undocumented event as power/exit. */
    if(life.last_poll && now>life.last_poll && now-life.last_poll>=2000){++life.resume_epoch;psmtp_lifecycle_mark("system-activity-gap-revalidate",0);}
    life.last_poll=now;
    if(!life.status)return;memset(&status,0,sizeof(status));rc=life.status(&status);
    if(rc){if(rc!=life.status_error)psmtp_lifecycle_mark("system-status-error",rc);life.status_error=rc;__atomic_store_n(&life.pause,1,__ATOMIC_RELEASE);return;}
    life.status_error=0;memcpy(&event_count,status.bytes,4);overlay=status.bytes[4]!=0;background=status.bytes[5]!=0;
    {int paused=overlay || background;if(life.pause && !paused)++life.resume_epoch;__atomic_store_n(&life.pause,paused,__ATOMIC_RELEASE);}
    if(overlay!=life.last_overlay || background!=life.last_background){
        psmtp_lifecycle_mark("system-ui-overlay",overlay);psmtp_lifecycle_mark("system-background",background);
        life.last_overlay=overlay;life.last_background=background;
    }
    if(!life.last_heartbeat || now-life.last_heartbeat>=5000){psmtp_lifecycle_mark("main-checkpoint-alive",background);life.last_heartbeat=now;}
    /* Record known event ABI, without inventing an EXIT_REQUESTED event.
     * Resume, invitations, etc. are diagnostics only, never exit triggers. */
    for(int i=0;life.event && i<event_count && i<8;++i){int32_t type=0;memset(&event,0,sizeof(event));rc=life.event(&event);if(rc)break;memcpy(&type,event.bytes,4);psmtp_lifecycle_mark("system-event-type",type);
        /* Any queued event is a reason to revalidate, never to fabricate an
         * exit/suspend type. Existing status and native signals own intent. */
        ++life.resume_epoch;
    }
}
void psmtp_lifecycle_poll(void){int saved_errno=errno;lifecycle_poll_impl();errno=saved_errno;}
void psmtp_lifecycle_transfer_state(int *pause,unsigned *epoch){*pause=__atomic_load_n(&life.pause,__ATOMIC_ACQUIRE);*epoch=life.resume_epoch;}
void psmtp_lifecycle_close(void){
    int fd=__atomic_exchange_n(&life.fd,-1,__ATOMIC_ACQ_REL);
    if(fd>=0){
#ifndef PSMTP_PRODUCTION
        if(life.closing){unsigned count=__atomic_load_n(&life.shutdown_count,__ATOMIC_ACQUIRE),i;char bytes[16384];size_t used=0,done=0;
            if(count>128)count=128;
            for(i=0;i<count;++i)if(__atomic_load_n(&life.shutdown_records[i].ready,__ATOMIC_ACQUIRE)){
                int n=snprintf(bytes+used,sizeof(bytes)-used,"pid=%d shutdown-seq=%u stage=%s status=%d\n",(int)getpid(),i,life.shutdown_records[i].stage,life.shutdown_records[i].status);
                if(n<=0 || (size_t)n>=sizeof(bytes)-used)break;used+=(size_t)n;
            }
            /* A single final journal commit, bounded syscall attempts. No
             * per-subsystem sync logging or journal FD across LoadExec. */
            for(unsigned attempt=0;attempt<8 && done<used;++attempt){ssize_t n=write(fd,bytes+done,used-done);if(n<0 && errno==EINTR)continue;if(n<=0)break;done+=(size_t)n;}
        }
#endif
        (void)fsync(fd);(void)close(fd);
    }
}

int psmtp_lifecycle_join(void *thread,void **result,unsigned timeout_ms){
    struct timespec deadline;
    /* Native disassembly: argument 3 is dereferenced as sec/nsec, not usec.
     * This is the absolute timespec ABI of FreeBSD pthread_timedjoin_np. */
    if(!life.timedjoin || clock_gettime(CLOCK_REALTIME,&deadline))return -1;
    deadline.tv_sec+=(time_t)(timeout_ms/1000);deadline.tv_nsec+=(long)(timeout_ms%1000)*1000000;
    if(deadline.tv_nsec>=1000000000){++deadline.tv_sec;deadline.tv_nsec-=1000000000;}
    return life.timedjoin(thread,result,&deadline);
}
