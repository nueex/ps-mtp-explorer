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
            current->string_index=p[at+8];
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
    size_t i,j; unsigned candidates=0;int rank=0;
    memset(out,0,sizeof(*out));
    if(n>USB_MAX_ALTS) return -3;
    for(i=0;i<n;++i) {
        const usb_alt *a=alts+i; usb_mtp_interface v={0}; int bad=0;
        if(a->endpoint_count>USB_MAX_ENDPOINTS) return -3;


        int priority=(a->cls==6 && a->subclass==1 && a->protocol==1)?2:(a->cls==0xff && a->mtp_identified)?1:0;
        if(!priority) continue;
        v.interface_number=a->number;v.alternate=a->alternate;
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
        if(!bad && v.bulk_in && v.bulk_out && v.interrupt_in) {
            if(priority>rank){*out=v;rank=priority;candidates=1;}
            else if(priority==rank){
                if(out->interface_number!=v.interface_number)++candidates;
                else if(v.alternate<out->alternate)*out=v;
            }
        }
    }
    if(candidates!=1) { memset(out,0,sizeof(*out)); return candidates?-2:-1; }
    return 0;
}
int usb_mtp_os_vendor_code(const uint8_t *p,size_t n,uint8_t *code){
    static const uint8_t signature[14]={'M',0,'S',0,'F',0,'T',0,'1',0,'0',0,'0',0};
    *code=0;
    if(n!=18 || p[0]!=18 || p[1]!=3 || memcmp(p+2,signature,sizeof(signature)) || !p[16] || p[17])return -3;
    *code=p[16];return 0;
}
int usb_mtp_compatible_interfaces(const uint8_t *p,size_t n,uint8_t out[256]){
    uint8_t seen[256]={0},found[256]={0};size_t i;uint32_t length;
    memset(out,0,256);
    if(n<16 || n>USB_MAX_CONFIG_BYTES)return -3;
    length=(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
    if(length!=n || n!=16+(size_t)p[8]*24 || p[4]!=0 || p[5]!=1 || p[6]!=4 || p[7])return -3;
    for(i=0;i<p[8];++i){const uint8_t *f=p+16+i*24;unsigned number=f[0];
        if(seen[number])return -3;seen[number]=1;
        if(!memcmp(f+2,"MTP\0\0\0\0\0",8))found[number]=1;
    }
    memcpy(out,found,256);return 0;
}
