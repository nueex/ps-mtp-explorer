/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#include "psmtp/keyboard.h"
static const unsigned starts[]={0,12,22,31,41},counts[]={12,10,9,10,3};
static unsigned row(size_t key){unsigned r=0;while(r<4 && key>=starts[r+1])++r;return r;}
int psmtp_key_action(size_t key){return key==11?PSMTP_KEY_BACKSPACE:key==31?PSMTP_KEY_SHIFT:key==41?PSMTP_KEY_CLEAR:key==43?PSMTP_KEY_SAVE:PSMTP_KEY_CHARACTER;}
char psmtp_key_character(size_t key,int shift){const char *digits="1234567890-",*top="qwertyuiop",*home="asdfghjkl",*bottom="zxcvbnm._";char c=0;
    if(key<11)c=digits[key];else if(key>=12 && key<22)c=top[key-12];else if(key>=22 && key<31)c=home[key-22];else if(key>=32 && key<41)c=bottom[key-32];else if(key==42)c=' ';
    if(shift && c>='a' && c<='z')c=(char)(c-'a'+'A');return c;
}
void psmtp_key_rect(size_t key,int *x,int *y,int *w,int *h){unsigned r=row(key),c=(unsigned)key-starts[r];*y=400+(int)r*71;*h=56;
    if(r==4){*x=c==0?408:c==1?610:1320;*w=c==1?692:184;}
    else {*w=r==0?84:88;*x=(r==0?408:r==1?480:r==2?524:480)+(int)c*(r==0?92:96);}
}
size_t psmtp_key_move(size_t key,int dx,int dy){unsigned r=row(key),c=(unsigned)key-starts[r];
    if(dx){if(dx<0 && c)--c;else if(dx>0 && c+1<counts[r])++c;return starts[r]+c;}
    if(dy){int x,y,w,h,bx,by,bw,bh;unsigned next=dy<0?(r?r-1:r):(r<4?r+1:r),i,best=0;int distance=10000;psmtp_key_rect(key,&x,&y,&w,&h);
        for(i=0;i<counts[next];++i){int d;psmtp_key_rect(starts[next]+i,&bx,&by,&bw,&bh);d=(bx+bw/2)-(x+w/2);if(d<0)d=-d;if(d<distance){distance=d;best=i;}}return starts[next]+best;
    }return key;
}
