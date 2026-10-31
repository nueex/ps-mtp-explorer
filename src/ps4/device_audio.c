/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#include "lifecycle.h"
#include "device_audio.h"
#include "psmtp/device_watch.h"
#include "psmtp/device_sound.h"
#ifdef PSMTP_TEST_AUDIO
#include "ps4_audio_test_api.h"
#else
#include <orbis/libkernel.h>
#include <orbis/AudioOut.h>
#include <orbis/UserService.h>
#endif
#include "native_sync.h"
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
/* The worker owns PCM and playback only; it never touches the UI, USB, logs
 * or filesystem. Native output may block, so it must not run on the UI thread.
 * C99-compatible compiler atomics keep this tiny event mailbox race-free. */
#include "psmtp/cleanup_log.h"

static struct {psmtp_sync changed;int port,started,reported;int stop,pending,error,done;unsigned played,reported_played;OrbisPthread thread;} audio={.port=-1};
static int stopped(void) {return psmtp_lifecycle_requested() || __atomic_load_n(&audio.stop,__ATOMIC_ACQUIRE);}
static void *playback(void *unused) {
    static int16_t pcm[PSMTP_CHIME_BLOCK*2];unsigned event=0;size_t frame=0;(void)unused;
    while(!stopped()) {
        int32_t rc;
        if(!event){unsigned pending=(unsigned)__atomic_exchange_n(&audio.pending,0,__ATOMIC_ACQ_REL);if(pending){event=pending&PSMTP_DEVICE_REMOVED?PSMTP_DEVICE_REMOVED:PSMTP_DEVICE_ADDED;frame=0;pending&=~event;if(pending)__atomic_fetch_or(&audio.pending,(int)pending,__ATOMIC_RELEASE);}}
        if(event)psmtp_chime_render(event==PSMTP_DEVICE_ADDED,frame,pcm,PSMTP_CHIME_BLOCK);else memset(pcm,0,sizeof(pcm));
        /* Continuous silence keeps the HDMI/main stream awake between cues.
         * Wait for consumption before overwriting this single PCM buffer. */
        rc=sceAudioOutOutput(audio.port,pcm);if(rc>=0)rc=sceAudioOutOutput(audio.port,NULL);
        if(rc<0){__atomic_store_n(&audio.error,rc,__ATOMIC_RELEASE);break;}
        if(event){frame+=PSMTP_CHIME_BLOCK;if(frame>=PSMTP_CHIME_FRAMES){event=0;__atomic_fetch_add(&audio.played,1,__ATOMIC_RELEASE);}}
    }__atomic_store_n(&audio.done,1,__ATOMIC_RELEASE);psmtp_sync_notify(&audio.changed);return NULL;
}
void psmtp_audio_open(FILE *log) {
    int32_t rc,user=255;int32_t volumes[8]={32768,32768,32768,32768,32768,32768,32768,32768};
    if(audio.started || audio.port>=0) return;
    rc=sceAudioOutInit();fprintf(log,"stage=audio-init status=0x%08" PRIx32 "\n",(uint32_t)rc);if(rc && (uint32_t)rc!=UINT32_C(0x8026000e)) return;
    audio.port=sceAudioOutOpen(user,ORBIS_AUDIO_OUT_PORT_TYPE_MAIN,0,PSMTP_CHIME_BLOCK,PSMTP_CHIME_RATE,ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
    fprintf(log,"stage=audio-open status=%d rate=%u frames=%u\n",audio.port,PSMTP_CHIME_RATE,PSMTP_CHIME_BLOCK);if(audio.port<0) return;
    rc=sceAudioOutSetVolume(audio.port,3,volumes);fprintf(log,"stage=audio-volume status=0x%08" PRIx32 " user=system route=main-stereo\n",(uint32_t)rc);
    if(rc<0){if(!sceAudioOutClose(audio.port))audio.port=-1;return;}
    if(psmtp_sync_init(&audio.changed)){if(!sceAudioOutClose(audio.port))audio.port=-1;return;}
    audio.stop=audio.pending=audio.error=audio.reported=audio.done=0;audio.played=audio.reported_played=0;
    rc=scePthreadCreate(&audio.thread,NULL,playback,NULL,"psmtp-device-audio");
    fprintf(log,"stage=audio-worker status=0x%08" PRIx32 "\n",(uint32_t)rc);
    if(rc) {psmtp_sync_destroy(&audio.changed);int32_t closed=sceAudioOutClose(audio.port);fprintf(log,"stage=audio-close status=0x%08" PRIx32 "\n",(uint32_t)closed);if(!closed)audio.port=-1;return;}
    audio.started=1;
}
void psmtp_audio_status(FILE *log) {
    int error=__atomic_load_n(&audio.error,__ATOMIC_ACQUIRE);unsigned played=__atomic_load_n(&audio.played,__ATOMIC_ACQUIRE);
    if(played!=audio.reported_played){audio.reported_played=played;fprintf(log,"stage=audio-cue-completed count=%u route=main-stereo\n",played);}
    if(error && !audio.reported) {audio.reported=1;fprintf(log,"stage=audio-output status=0x%08" PRIx32 " sound-disabled=1\n",(uint32_t)error);}
}
void psmtp_audio_notify(unsigned events,FILE *log) {
    events&=PSMTP_DEVICE_ADDED|PSMTP_DEVICE_REMOVED;
    psmtp_audio_status(log);
    if(!events) return;
    if(audio.started && __atomic_load_n(&audio.error,__ATOMIC_ACQUIRE))psmtp_audio_close(log);
    if(!audio.started)psmtp_audio_open(log);
    if(audio.started && !__atomic_load_n(&audio.error,__ATOMIC_ACQUIRE)) __atomic_fetch_or(&audio.pending,(int)events,__ATOMIC_RELEASE);
    fprintf(log,"stage=device-sound added=%d removed=%d enabled=%d\n",!!(events&PSMTP_DEVICE_ADDED),!!(events&PSMTP_DEVICE_REMOVED),audio.started && !audio.reported);
}
void psmtp_audio_stop(void){__atomic_store_n(&audio.stop,1,__ATOMIC_RELEASE);__atomic_store_n(&audio.pending,0,__ATOMIC_RELEASE);psmtp_sync_wake(&audio.changed);}
int psmtp_audio_close(FILE *log) {
    unsigned waits;int32_t rc;if(!audio.started){if(audio.port<0)return 0;rc=sceAudioOutClose(audio.port);if(!rc)audio.port=-1;return rc;}
    psmtp_audio_stop();
    for(waits=0;waits<200 && !__atomic_load_n(&audio.done,__ATOMIC_ACQUIRE);++waits)(void)psmtp_sync_wait_event(&audio.changed,10000);
    /* Closing the port from under sceAudioOutOutput or cancelling that call
     * would race its buffer ownership. Escalate to system process exit. */
    if(!__atomic_load_n(&audio.done,__ATOMIC_ACQUIRE)){fputs("stage=audio-drain timeout resources-retained=1\n",log);return -1;}
#ifdef PSMTP_TEST_AUDIO
    rc=scePthreadJoin(audio.thread,NULL);
#else
    rc=psmtp_lifecycle_join((void *)audio.thread,NULL,1000);
#endif
    fprintf(log,"stage=audio-join status=0x%08" PRIx32 "\n",(uint32_t)rc);
    if(rc) return rc;
    psmtp_audio_status(log);audio.started=0;psmtp_sync_destroy(&audio.changed);
    rc=sceAudioOutClose(audio.port);fprintf(log,"stage=audio-close status=0x%08" PRIx32 "\n",(uint32_t)rc);if(!rc)audio.port=-1;return rc;
}
