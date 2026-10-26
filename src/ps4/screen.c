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
#include <orbis/libkernel.h>
#include <orbis/VideoOut.h>
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "console_render.h"
#include "ui_font.h"
#include "ui_draw.h"
#include <time.h>

#define WIDTH PSMTP_SCREEN_WIDTH
#define HEIGHT PSMTP_SCREEN_HEIGHT
#define FRAME_BYTES (WIDTH*HEIGHT*4u)
#define DISPLAY_BYTES ((FRAME_BYTES*2u+0x1fffffu)&~0x1fffffu)
static struct {
    int video,pad,registered,mapped,allocated,frame;
    off_t offset; void *memory; void *buffers[2]; int64_t flip;
    uint32_t buttons; uint64_t repeat_at; int pad_error,pad_connected; int32_t display_error;
    uint32_t *cpu;FILE *log;uint64_t timing_started,render_us;unsigned frames;
} screen={.video=-1,.pad=-1};
int psmtp_screen_open(FILE *log) {
    int32_t rc,user=-1; OrbisVideoOutBufferAttribute attr;
    OrbisUserServiceInitializeParams params={ORBIS_KERNEL_PRIO_FIFO_LOWEST};
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
    screen.cpu=malloc(FRAME_BYTES);if(!screen.cpu)return -1;screen.log=log;
    rc=sceVideoOutSetFlipRate(screen.video,0);
    fprintf(log,"stage=video-flip-rate rc=0x%08" PRIx32 "\n",(uint32_t)rc); if(rc) return -1;
    rc=scePadInit(); fprintf(log,"stage=pad-init rc=0x%08" PRIx32 "\n",(uint32_t)rc);
    if(rc) return 0;
    rc=sceUserServiceInitialize(&params); fprintf(log,"stage=user-init rc=0x%08" PRIx32 "\n",(uint32_t)rc);
    rc=sceUserServiceGetInitialUser(&user); fprintf(log,"stage=initial-user rc=0x%08" PRIx32 "\n",(uint32_t)rc);
    if(rc) return 0;
    screen.pad=scePadOpen(user,0,0,NULL); fprintf(log,"stage=pad-open rc=%d\n",screen.pad);
    return 0;
}
void psmtp_screen_draw(const diag_log *log,size_t scroll,const char *status,int names,const char *save_status,const psmtp_console_selection *selection,const mtp_navigator *browser) {
    unsigned tries; int32_t rc;
    if(!screen.registered || screen.display_error) return;
#ifndef PSMTP_PRODUCTION
    {struct timespec ts;uint64_t start,finish;clock_gettime(CLOCK_MONOTONIC,&ts);start=(uint64_t)ts.tv_sec*1000000+ts.tv_nsec/1000;
#endif
    psmtp_console_render(screen.cpu,log,scroll,status,names,save_status,selection,browser);
    memcpy(screen.buffers[screen.frame],screen.cpu,FRAME_BYTES);
#ifndef PSMTP_PRODUCTION
    clock_gettime(CLOCK_MONOTONIC,&ts);finish=(uint64_t)ts.tv_sec*1000000+ts.tv_nsec/1000;screen.render_us+=finish-start;
    if(!screen.timing_started)screen.timing_started=start;++screen.frames;
    if(finish-screen.timing_started>=5000000){
        unsigned frames=screen.frames;uint64_t elapsed=finish-screen.timing_started,average=screen.render_us/frames;

        screen.frames=0;screen.render_us=0;screen.timing_started=finish;
        fprintf(screen.log,"stage=render-timing frames=%u elapsed-us=%" PRIu64 " average-render-us=%" PRIu64 " view=%d\n",frames,elapsed,average,selection?selection->view:-1);
    }}
#endif
    rc=sceVideoOutSubmitFlip(screen.video,screen.frame,ORBIS_VIDEO_OUT_FLIP_VSYNC,++screen.flip);
    if(rc) { screen.display_error=rc; return; }
    for(tries=0;tries<500;++tries) {
        OrbisVideoOutFlipStatus fs; memset(&fs,0,sizeof(fs));
        rc=sceVideoOutGetFlipStatus(screen.video,&fs);
        if(rc) { screen.display_error=rc; return; }
        if(fs.flipArg==screen.flip) break;
        sceKernelUsleep(1000);
    }
    if(tries==500) { screen.display_error=-1; return; }
    screen.frame^=1;
}
uint32_t psmtp_screen_buttons(void) {
    OrbisPadData pad; uint32_t pressed,held,nav; int32_t rc; struct timespec ts={0}; uint64_t now;
    if(screen.pad<0 || screen.pad_error) return 0;
    memset(&pad,0,sizeof(pad)); rc=scePadReadState(screen.pad,&pad);
    if(rc<0) { screen.pad_error=rc; screen.pad_connected=0; return 0; }
    screen.pad_connected=pad.connected!=0;
    if(!pad.connected) { screen.buttons=0; return 0; }
    held=pad.buttons;
    if(pad.leftStick.y<64) held|=ORBIS_PAD_BUTTON_UP;
    if(pad.leftStick.y>192) held|=ORBIS_PAD_BUTTON_DOWN;
    nav=held&(ORBIS_PAD_BUTTON_UP|ORBIS_PAD_BUTTON_DOWN|ORBIS_PAD_BUTTON_LEFT|ORBIS_PAD_BUTTON_RIGHT|ORBIS_PAD_BUTTON_L1|ORBIS_PAD_BUTTON_R1);
    pressed=held&~screen.buttons;
    now=clock_gettime(CLOCK_MONOTONIC,&ts)?0:(uint64_t)ts.tv_sec*1000+(uint64_t)ts.tv_nsec/1000000;
    if(pressed&nav) screen.repeat_at=now+360;
    else if(nav && now>=screen.repeat_at) {pressed|=nav;screen.repeat_at=now+90;}
    screen.buttons=held; return pressed;
}
int psmtp_screen_pad_ready(void) { return screen.pad>=0 && !screen.pad_error; }
int psmtp_screen_pad_connected(void) { return psmtp_screen_pad_ready() && screen.pad_connected; }
int32_t psmtp_screen_pad_error(void) { return screen.pad_error; }
int32_t psmtp_screen_error(void) { return screen.display_error; }
void psmtp_screen_close(FILE *log) {
    int32_t rc;
    psmtp_font_close();
    ui_draw_close();
    free(screen.cpu);screen.cpu=NULL;
    if(screen.pad>=0) { rc=scePadClose(screen.pad); fprintf(log,"stage=pad-close rc=0x%08" PRIx32 "\n",(uint32_t)rc); }
    if(screen.registered) { rc=sceVideoOutUnregisterBuffers(screen.video,0); fprintf(log,"stage=video-unregister rc=0x%08" PRIx32 "\n",(uint32_t)rc); }
    if(screen.video>=0) {
        rc=sceVideoOutClose(screen.video); fprintf(log,"stage=video-close rc=0x%08" PRIx32 "\n",(uint32_t)rc);
        if(rc) { fputs("Display memory retained until process exit because video close failed.\n",log); return; }
    }
    if(screen.mapped) {
        rc=sceKernelMunmap(screen.memory,DISPLAY_BYTES); fprintf(log,"stage=video-unmap rc=0x%08" PRIx32 "\n",(uint32_t)rc);
        if(rc) return;
    }
    if(screen.allocated) { rc=sceKernelReleaseDirectMemory(screen.offset,DISPLAY_BYTES); fprintf(log,"stage=video-release rc=0x%08" PRIx32 "\n",(uint32_t)rc); }
}
