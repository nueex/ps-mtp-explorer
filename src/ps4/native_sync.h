/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PSMTP_NATIVE_SYNC_H
#define PSMTP_NATIVE_SYNC_H
#include <string.h>
#if defined(PSMTP_TEST_PREVIEW) || defined(PSMTP_TEST_AUDIO)
/* Deterministic scheduler models; real-thread contracts use the host branch. */
typedef struct {int ready;} psmtp_sync;
static inline int psmtp_sync_init(psmtp_sync *s){s->ready=1;return 0;}
static inline int psmtp_sync_lock(psmtp_sync *s){(void)s;return 0;}
static inline int psmtp_sync_trylock(psmtp_sync *s){(void)s;return 0;}
static inline void psmtp_sync_unlock(psmtp_sync *s){(void)s;}
static inline int psmtp_sync_wait(psmtp_sync *s,unsigned us){(void)s;return sceKernelUsleep(us);}
static inline void psmtp_sync_signal_locked(psmtp_sync *s){(void)s;}
static inline void psmtp_sync_destroy(psmtp_sync *s){s->ready=0;}
#elif defined(PSMTP_TEST_WORK) || defined(PSMTP_TEST_DOWNLOAD_PIPELINE) || defined(PSMTP_TEST_PHONE_SERVER)
#ifdef _WIN32
#include <windows.h>
typedef struct {CRITICAL_SECTION mutex;CONDITION_VARIABLE condition;int ready;} psmtp_sync;
static inline int psmtp_sync_init(psmtp_sync *s){InitializeCriticalSection(&s->mutex);InitializeConditionVariable(&s->condition);s->ready=1;return 0;}
static inline int psmtp_sync_lock(psmtp_sync *s){EnterCriticalSection(&s->mutex);return 0;}
static inline int psmtp_sync_trylock(psmtp_sync *s){return TryEnterCriticalSection(&s->mutex)?0:1;}
static inline void psmtp_sync_unlock(psmtp_sync *s){LeaveCriticalSection(&s->mutex);}
static inline int psmtp_sync_wait(psmtp_sync *s,unsigned us){if(SleepConditionVariableCS(&s->condition,&s->mutex,(us+999)/1000))return 0;return GetLastError()==ERROR_TIMEOUT?0:-1;}
static inline void psmtp_sync_signal_locked(psmtp_sync *s){WakeAllConditionVariable(&s->condition);}
static inline void psmtp_sync_destroy(psmtp_sync *s){if(s->ready)DeleteCriticalSection(&s->mutex);s->ready=0;}
#else
#include <pthread.h>
#include <time.h>
#include <errno.h>
typedef struct {pthread_mutex_t mutex;pthread_cond_t condition;int ready;} psmtp_sync;
static inline int psmtp_sync_init(psmtp_sync *s){int rc=pthread_mutex_init(&s->mutex,NULL);if(rc)return rc;rc=pthread_cond_init(&s->condition,NULL);if(rc){pthread_mutex_destroy(&s->mutex);return rc;}s->ready=1;return 0;}
static inline int psmtp_sync_lock(psmtp_sync *s){return pthread_mutex_lock(&s->mutex);}
static inline int psmtp_sync_trylock(psmtp_sync *s){return pthread_mutex_trylock(&s->mutex);}
static inline void psmtp_sync_unlock(psmtp_sync *s){pthread_mutex_unlock(&s->mutex);}
static inline int psmtp_sync_wait(psmtp_sync *s,unsigned us){struct timespec t;int rc;clock_gettime(CLOCK_REALTIME,&t);t.tv_nsec+=(long)us*1000;t.tv_sec+=t.tv_nsec/1000000000;t.tv_nsec%=1000000000;rc=pthread_cond_timedwait(&s->condition,&s->mutex,&t);return rc==ETIMEDOUT?0:rc;}
static inline void psmtp_sync_signal_locked(psmtp_sync *s){pthread_cond_broadcast(&s->condition);}
static inline void psmtp_sync_destroy(psmtp_sync *s){if(s->ready){pthread_cond_destroy(&s->condition);pthread_mutex_destroy(&s->mutex);}s->ready=0;}
#endif
#else
#include <orbis/libkernel.h>
#include <orbis/_types/errors.h>
typedef struct {OrbisPthreadMutex mutex;OrbisPthreadCond condition;int ready;} psmtp_sync;
static inline int psmtp_sync_init(psmtp_sync *s){int rc;memset(s,0,sizeof(*s));rc=scePthreadMutexInit(&s->mutex,NULL,"psmtp-io");if(rc)return rc;rc=scePthreadCondInit(&s->condition,NULL,"psmtp-io");if(rc){scePthreadMutexDestroy(&s->mutex);return rc;}s->ready=1;return 0;}
static inline int psmtp_sync_lock(psmtp_sync *s){return scePthreadMutexLock(&s->mutex);}
static inline int psmtp_sync_trylock(psmtp_sync *s){return scePthreadMutexTrylock(&s->mutex);}
static inline void psmtp_sync_unlock(psmtp_sync *s){(void)scePthreadMutexUnlock(&s->mutex);}
static inline int psmtp_sync_wait(psmtp_sync *s,unsigned us){int rc=scePthreadCondTimedwait(&s->condition,&s->mutex,us);return (uint32_t)rc==ORBIS_KERNEL_ERROR_ETIMEDOUT?0:rc;}
static inline void psmtp_sync_signal_locked(psmtp_sync *s){(void)scePthreadCondBroadcast(&s->condition);}
static inline void psmtp_sync_destroy(psmtp_sync *s){if(s->ready){(void)scePthreadCondDestroy(&s->condition);(void)scePthreadMutexDestroy(&s->mutex);}s->ready=0;}
#endif
static inline void psmtp_sync_notify(psmtp_sync *s){if(s->ready && !psmtp_sync_lock(s)){psmtp_sync_signal_locked(s);psmtp_sync_unlock(s);}}
/* Stop predicates are atomic; a broadcast needs no user mutex. A lost wake
 * remains bounded by the wait timeout, and shutdown never waits for that lock. */
static inline void psmtp_sync_wake(psmtp_sync *s){if(s->ready)psmtp_sync_signal_locked(s);}
static inline int psmtp_sync_wait_event(psmtp_sync *s,unsigned us){int rc=psmtp_sync_lock(s);if(rc)return rc;rc=psmtp_sync_wait(s,us);psmtp_sync_unlock(s);return rc;}
#endif
