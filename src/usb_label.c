/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#include "psmtp/usb_label.h"
#include "psmtp/mtp.h"
int psmtp_usb_label(const uint8_t *p,size_t n,char *out,size_t cap) {
    size_t i,used=0;int full=0;
    if(!out || !cap) return MTP_STATE;
    out[0]=0;
    if(!p || n<2 || p[1]!=3 || p[0]<2 || p[0]>n || (p[0]&1)) return MTP_PROTOCOL;
    for(i=2;i<p[0];i+=2) {
        uint32_t c=(uint32_t)p[i]|(uint32_t)p[i+1]<<8;uint8_t utf[4];size_t k=0,j;
        if(c>=0xd800 && c<=0xdbff) {
            uint32_t low;if(i+3>=p[0]) goto invalid;i+=2;low=(uint32_t)p[i]|(uint32_t)p[i+1]<<8;
            if(low<0xdc00 || low>0xdfff) goto invalid;
            c=0x10000+((c-0xd800)<<10)+(low-0xdc00);
        } else if(c>=0xdc00 && c<=0xdfff) goto invalid;
        if(c<32 || (c>=0x7f && c<=0x9f) || c==0x2028 || c==0x2029) goto invalid;
        if(c<0x80) utf[k++]=(uint8_t)c;
        else if(c<0x800) {utf[k++]=(uint8_t)(0xc0|(c>>6));utf[k++]=(uint8_t)(0x80|(c&63));}
        else if(c<0x10000) {utf[k++]=(uint8_t)(0xe0|(c>>12));utf[k++]=(uint8_t)(0x80|((c>>6)&63));utf[k++]=(uint8_t)(0x80|(c&63));}
        else {utf[k++]=(uint8_t)(0xf0|(c>>18));utf[k++]=(uint8_t)(0x80|((c>>12)&63));utf[k++]=(uint8_t)(0x80|((c>>6)&63));utf[k++]=(uint8_t)(0x80|(c&63));}
        if(used+k>=cap) full=1;
        if(!full) for(j=0;j<k;++j) out[used++]=(char)utf[j];
    }
    out[used]=0;return used?0:MTP_PROTOCOL;
invalid:
    out[0]=0;return MTP_PROTOCOL;
}
