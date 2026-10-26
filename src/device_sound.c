/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#include "psmtp/device_sound.h"
static const int16_t sine[65]={
    0,3212,6393,9512,12539,15446,18204,20787,23170,25329,27245,28898,30273,31356,32137,32609,
    32767,32609,32137,31356,30273,28898,27245,25329,23170,20787,18204,15446,12539,9512,6393,3212,
    0,-3212,-6393,-9512,-12539,-15446,-18204,-20787,-23170,-25329,-27245,-28898,-30273,-31356,-32137,-32609,
    -32767,-32609,-32137,-31356,-30273,-28898,-27245,-25329,-23170,-20787,-18204,-15446,-12539,-9512,-6393,-3212,0};


static int32_t tone(unsigned hz,size_t frame) {
    uint64_t phase=(uint64_t)frame*hz*64*65536/PSMTP_CHIME_RATE;
    unsigned index=(unsigned)(phase>>16)&63u,fraction=(unsigned)phase&65535u;
    return sine[index]+(int32_t)((int64_t)(sine[index+1]-sine[index])*fraction/65536);
}
void psmtp_chime_render(int connected,size_t frame,int16_t *out,size_t frames) {
    static const unsigned notes[2][3]={{784,659,440},{659,880,1109}};
    size_t i;
    for(i=0;i<frames;++i) {
        size_t position=frame+i;int32_t left=0,right=0;unsigned voice;
        for(voice=0;voice<3 && position<PSMTP_CHIME_FRAMES;++voice) {
            size_t delay=voice*2400u,local,length=PSMTP_CHIME_FRAMES-delay;int64_t envelope,value;
            if(position<delay)continue;local=position-delay;
            envelope=(int64_t)(length-1-local)*32768/(int64_t)length;
            envelope=envelope*envelope/32768*envelope/32768;
            if(local<240)envelope=envelope*(int64_t)local/240;
            value=(tone(notes[!!connected][voice],local)*8+tone(notes[!!connected][voice]*2,local)+tone(notes[!!connected][voice]*3,local))/10;
            value=value*envelope*(voice==0?3000:2200)/(32768*32767LL);
            left+=(int32_t)(value*(voice==2?7:10)/10);right+=(int32_t)(value*(voice==0?7:10)/10);
        }
        out[i*2]=(int16_t)left;out[i*2+1]=(int16_t)right;
    }
}
