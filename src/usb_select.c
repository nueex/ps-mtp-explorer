/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#include "psmtp/usb_select.h"
#include <string.h>
int usb_parse_config(const uint8_t *p,size_t n,usb_alt *alts,size_t *count,uint8_t *value) {
    size_t at=0,k=0,seen=0; unsigned expected=0; usb_alt *current=NULL;
    unsigned char interfaces[256]={0}; unsigned distinct=0;
    *count=0;
    if(n<9 || n>USB_MAX_CONFIG_BYTES || p[0]!=9 || p[1]!=2 ||
       ((size_t)p[2]|(size_t)p[3]<<8)!=n || !p[5]) return -3;
    *value=p[5];
    while(at<n) {
        unsigned len=p[at],type;
        if(n-at<2 || len<2 || len>n-at) return -3;
        type=p[at+1];
        if(type==2 && at) return -3;
        if(type==4) {
            size_t j;
            if(len<9 || k==USB_MAX_ALTS || (current && seen!=expected)) return -3;
            for(j=0;j<k;++j) if(alts[j].number==p[at+2] && alts[j].alternate==p[at+3]) return -3;
            if(!interfaces[p[at+2]]) { interfaces[p[at+2]]=1; ++distinct; }
            current=alts+k++; memset(current,0,sizeof(*current));
            current->number=p[at+2]; current->alternate=p[at+3]; expected=p[at+4];
            current->cls=p[at+5]; current->subclass=p[at+6]; current->protocol=p[at+7];
            if(expected>USB_MAX_ENDPOINTS) return -3;
            seen=0;
        } else if(type==5) {
            usb_endpoint *ep; size_t j;
            if(!current || len<7 || seen>=expected) return -3;
            for(j=0;j<seen;++j) if(current->endpoints[j].address==p[at+2]) return -3;
            ep=current->endpoints+seen++;
            ep->address=p[at+2]; ep->attributes=p[at+3];
            ep->max_packet=(uint16_t)(p[at+4]|(uint16_t)p[at+5]<<8);
            current->endpoint_count=seen;
        }
        at+=len;
    }
    if((current && seen!=expected) || distinct!=p[4]) return -3;
    *count=k; return 0;
}
int usb_select_mtp(const usb_alt *alts,size_t n,usb_mtp_interface *out) {
    size_t i,j; unsigned candidates=0;
    memset(out,0,sizeof(*out));
    if(n>USB_MAX_ALTS) return -3;
    for(i=0;i<n;++i) {
        const usb_alt *a=alts+i; usb_mtp_interface v={0}; int bad=0;
        if(a->endpoint_count>USB_MAX_ENDPOINTS) return -3;


        if(a->cls!=6 || a->subclass!=1 || a->protocol!=1 || a->alternate!=0) continue;
        v.interface_number=a->number;
        for(j=0;j<a->endpoint_count;++j) {
            const usb_endpoint *ep=a->endpoints+j;
            uint8_t kind=ep->attributes&3;
            if(!(ep->address&15) || (ep->address&0x70) || !ep->max_packet || ep->max_packet>1024) { bad=1; break; }
            if(kind==2) {
                uint8_t *target=(ep->address&0x80)?&v.bulk_in:&v.bulk_out;
                if(!(ep->address&0x80))v.out_max_packet=ep->max_packet;
                else v.in_max_packet=ep->max_packet;
                if(*target) bad=1;
                *target=ep->address;
            } else if(kind==3 && (ep->address&0x80)) {
                if(v.interrupt_in) bad=1;
                v.interrupt_in=ep->address;
            } else bad=1;
        }
        if(!bad && v.bulk_in && v.bulk_out && v.interrupt_in) { *out=v; ++candidates; }
    }
    if(candidates!=1) { memset(out,0,sizeof(*out)); return candidates?-2:-1; }
    return 0;
}
