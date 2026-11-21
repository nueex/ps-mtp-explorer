/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#include "screen.h"
#include "controller.h"
#include "lifecycle.h"
#ifdef PSMTP_TEST_SCREEN
#include "ps4_screen_test_api.h"
#else
#include <orbis/GnmDriver.h>
#include <orbis/libkernel.h>
#include <orbis/VideoOut.h>
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "console_render.h"
#include "ui_font.h"
#include "ui_draw.h"
#include <time.h>
#include "monotonic.h"

#define WIDTH PSMTP_SCREEN_WIDTH
#define HEIGHT PSMTP_SCREEN_HEIGHT
#define FRAME_BYTES (WIDTH*HEIGHT*4u)
#define DISPLAY_BYTES ((FRAME_BYTES*2u+0x1fffffu)&~0x1fffffu)
#include "psmtp/cleanup_log.h"

static struct {
    int video,registered,mapped,allocated,frame;
    off_t offset; void *memory; void *buffers[2]; int64_t flip;
    int32_t display_error;
    uint32_t *cpu;FILE *log;uint64_t timing_started,render_us,upload_generation[2],uploaded_bytes;unsigned frames,cached_frames,full_uploads;
} screen={.video=-1};
int psmtp_screen_open(FILE *log) {
    int32_t rc; OrbisVideoOutBufferAttribute attr;
    if(screen.video>=0 || screen.mapped || screen.allocated)return -1;
    screen.video=sceVideoOutOpen(ORBIS_VIDEO_USER_MAIN,ORBIS_VIDEO_OUT_BUS_MAIN,0,NULL);
    fprintf(log,"stage=video-open rc=%d\n",screen.video); if(screen.video<0) return -1;
    rc=sceKernelAllocateDirectMemory(0,sceKernelGetDirectMemorySize(),DISPLAY_BYTES,0x200000,3,&screen.offset);
    fprintf(log,"stage=video-allocate rc=0x%08" PRIx32 "\n",(uint32_t)rc); if(rc) return -1;
    screen.allocated=1;
    rc=sceKernelMapDirectMemory(&screen.memory,DISPLAY_BYTES,0x33,0,screen.offset,0x200000);
    fprintf(log,"stage=video-map rc=0x%08" PRIx32 "\n",(uint32_t)rc); if(rc) return -1;
    screen.mapped=1; screen.buffers[0]=screen.memory; screen.buffers[1]=(uint8_t *)screen.memory+FRAME_BYTES;
    memset(screen.memory,0,DISPLAY_BYTES); memset(&attr,0,sizeof(attr));
    sceVideoOutSetBufferAttribute(&attr,ORBIS_VIDEO_OUT_PIXEL_FORMAT_A8B8G8R8_SRGB,1,0,WIDTH,HEIGHT,WIDTH);
    rc=sceVideoOutRegisterBuffers(screen.video,0,screen.buffers,2,&attr);
    fprintf(log,"stage=video-register rc=0x%08" PRIx32 "\n",(uint32_t)rc); if(rc) return -1;
    screen.registered=1;
    screen.cpu=malloc(FRAME_BYTES);if(!screen.cpu)return -1;screen.log=log;screen.upload_generation[0]=screen.upload_generation[1]=0;
    rc=sceVideoOutSetFlipRate(screen.video,0);
    fprintf(log,"stage=video-flip-rate rc=0x%08" PRIx32 "\n",(uint32_t)rc); if(rc) return -1;
    psmtp_controller_open(log);
    return 0;
}
void psmtp_screen_submit_done(void) {
    /* The system waits for this acknowledgement before suspending the title.
     * Native video decoding can use the GPU even though this renderer is CPU-only. */
    if(screen.registered)(void)sceGnmSubmitDone();
}
void psmtp_screen_draw(const diag_log *log,size_t scroll,const char *status,int names,const char *save_status,const psmtp_console_selection *selection,const mtp_navigator *browser) {
    unsigned tries; int32_t rc;
    if(!screen.registered || screen.display_error) return;
#ifndef PSMTP_PRODUCTION
    {struct timespec ts;uint64_t start,finish;psmtp_monotonic_time(&ts);start=(uint64_t)ts.tv_sec*1000000+ts.tv_nsec/1000;
#endif
    psmtp_console_render(screen.cpu,log,scroll,status,names,save_status,selection,browser);
#ifndef PSMTP_PRODUCTION
    screen.cached_frames+=psmtp_console_render_cached()!=0;
#endif
    {size_t uploaded=psmtp_console_render_upload(screen.buffers[screen.frame],screen.cpu,&screen.upload_generation[screen.frame]);
#ifndef PSMTP_PRODUCTION
    screen.uploaded_bytes+=uploaded;screen.full_uploads+=uploaded==FRAME_BYTES;
#else
    (void)uploaded;
#endif
    /* Publish CPU stores, including partial write-combined buffer updates,
     * before submitting the next display flip. */
    __atomic_thread_fence(__ATOMIC_SEQ_CST);}
#ifndef PSMTP_PRODUCTION
    psmtp_monotonic_time(&ts);finish=(uint64_t)ts.tv_sec*1000000+ts.tv_nsec/1000;screen.render_us+=finish-start;
    if(!screen.timing_started)screen.timing_started=start;++screen.frames;
    if(finish-screen.timing_started>=5000000){
        unsigned frames=screen.frames,cached=screen.cached_frames,full=screen.full_uploads;uint64_t elapsed=finish-screen.timing_started,average=screen.render_us/frames,uploaded=screen.uploaded_bytes;
        /* Commit the new interval before invoking the application log sink. */
        screen.frames=screen.cached_frames=screen.full_uploads=0;screen.render_us=screen.uploaded_bytes=0;screen.timing_started=finish;
        fprintf(screen.log,"stage=render-timing frames=%u elapsed-us=%" PRIu64 " average-render-us=%" PRIu64 " view=%d cached-frames=%u upload-bytes=%" PRIu64 " full-uploads=%u\n",frames,elapsed,average,selection?selection->view:-1,cached,uploaded,full);
    }}
#endif
    psmtp_screen_submit_done();
#ifndef PSMTP_TEST_SCREEN
    if(psmtp_lifecycle_requested())return;
#endif
    rc=sceVideoOutSubmitFlip(screen.video,screen.frame,ORBIS_VIDEO_OUT_FLIP_VSYNC,++screen.flip);
    if(rc) { screen.display_error=rc; return; }
    for(tries=0;tries<500;++tries) {
#ifndef PSMTP_TEST_SCREEN
        if(psmtp_lifecycle_requested())return;
#endif
        OrbisVideoOutFlipStatus fs; memset(&fs,0,sizeof(fs));
        rc=sceVideoOutGetFlipStatus(screen.video,&fs);
        if(rc) { screen.display_error=rc; return; }
        if(fs.flipArg==screen.flip) break;
        if(!sceGnmAreSubmitsAllowed())psmtp_screen_submit_done();
        sceKernelUsleep(1000);
    }
    if(tries==500) { screen.display_error=-1; return; } /* local display deadline */
    screen.frame^=1;
}
uint32_t psmtp_screen_buttons(void){return psmtp_controller_buttons();}
uint32_t psmtp_screen_buttons_down(void){return psmtp_controller_down();}
int psmtp_screen_pad_ready(void){return psmtp_controller_ready();}
int psmtp_screen_pad_connected(void){return psmtp_controller_connected();}
int32_t psmtp_screen_pad_error(void){return psmtp_controller_error();}
int32_t psmtp_screen_error(void) { return screen.display_error; }
int psmtp_screen_shutdown(FILE *log,int release_user) {
    int32_t rc,first=0;
    psmtp_screen_submit_done();
    psmtp_console_render_close();
    psmtp_font_close();
    ui_draw_close();
    free(screen.cpu);screen.cpu=NULL;
    first=psmtp_controller_shutdown(log,release_user);
    if(screen.registered) { rc=sceVideoOutUnregisterBuffers(screen.video,0); fprintf(log,"stage=video-unregister rc=0x%08" PRIx32 "\n",(uint32_t)rc);if(!rc)screen.registered=0;else if(!first)first=rc; }
    if(screen.video>=0) {
        rc=sceVideoOutClose(screen.video); fprintf(log,"stage=video-close rc=0x%08" PRIx32 "\n",(uint32_t)rc);
        if(rc) { fputs("Display memory retained until process exit because video close failed.\n",log); return first?first:rc; }
        screen.video=-1;screen.registered=0;
    }
    if(screen.mapped) {
        rc=sceKernelMunmap(screen.memory,DISPLAY_BYTES); fprintf(log,"stage=video-unmap rc=0x%08" PRIx32 "\n",(uint32_t)rc);
        if(rc) return first?first:rc;
        screen.mapped=0;
    }
    if(screen.allocated) { rc=sceKernelReleaseDirectMemory(screen.offset,DISPLAY_BYTES); fprintf(log,"stage=video-release rc=0x%08" PRIx32 "\n",(uint32_t)rc);if(!rc)screen.allocated=0;else if(!first)first=rc; }
    screen.log=NULL;return first;
}
void psmtp_screen_close(FILE *log){(void)psmtp_screen_shutdown(log,1);}
