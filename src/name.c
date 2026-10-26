/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#include "psmtp/name.h"
#include <string.h>
int psmtp_name_encode(const char *name,uint8_t *out,size_t cap,size_t *length) {
    const unsigned char *p=(const unsigned char *)name;size_t units=0,n;uint8_t data[511];
    *length=0;if(!name || !(n=strlen(name)) || n>255 || name[n-1]==' ' || name[n-1]=='.' || !strcmp(name,".") || !strcmp(name,".."))return -1;
    while(*p){uint32_t cp=*p++;unsigned extra=0,i;if(cp<32 || cp==127 || (cp<128 && strchr("/\\:*?\"<>|",(int)cp)))return -1;
        if(cp>=128){if(cp>=0xc2 && cp<=0xdf){extra=1;cp&=31;}else if(cp>=0xe0 && cp<=0xef){extra=2;cp&=15;}else if(cp>=0xf0 && cp<=0xf4){extra=3;cp&=7;}else return -1;
            if((size_t)(p-(const unsigned char *)name)+extra>n)return -1;
            for(i=0;i<extra;++i){if((p[i]&0xc0)!=0x80)return -1;cp=(cp<<6)|(p[i]&63);}p+=extra;
            if((extra==1 && cp<128)||(extra==2 && cp<2048)||(extra==3 && cp<65536)||cp>0x10ffff||(cp>=0xd800 && cp<=0xdfff))return -1;}
        if(cp>=0x10000){uint32_t high=0xd800+((cp-0x10000)>>10);if(units>=252)return -1;data[1+units*2]=(uint8_t)high;data[2+units*2]=(uint8_t)(high>>8);++units;cp=0xdc00+((cp-0x10000)&1023);}
        if(units>=254)return -1;data[1+units*2]=(uint8_t)cp;data[2+units*2]=(uint8_t)(cp>>8);++units;
    }
    data[0]=(uint8_t)(units+1);data[1+units*2]=data[2+units*2]=0;*length=3+units*2;
    if(out){if(cap<*length)return -1;memcpy(out,data,*length);}return 0;
}
int psmtp_name_valid(const char *name){size_t n;return !psmtp_name_encode(name,NULL,0,&n);}
