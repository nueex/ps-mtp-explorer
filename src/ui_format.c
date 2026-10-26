/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#include "psmtp/ui_format.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
void psmtp_ui_size(char *out,size_t cap,uint64_t n) {
    const char *units[]={"B","KB","MB","GB","TB","PB","EB"};
    unsigned u=0; uint64_t divisor=1;
    while(u<6 && n/divisor>=1024) { divisor*=1024; ++u; }
    if(!u) snprintf(out,cap,"%llu B",(unsigned long long)n);
    else {
        uint64_t whole=n/divisor,part=((n%divisor)*10)/divisor;
        if(part>9) part=9;
        snprintf(out,cap,"%llu.%u %s",(unsigned long long)whole,(unsigned)part,units[u]);
    }
}
int psmtp_ui_name_compare(const char *a,const char *b) {
    while(*a && *b) { int x=tolower((unsigned char)*a++),y=tolower((unsigned char)*b++); if(x!=y) return x-y; }
    return (unsigned char)*a-(unsigned char)*b;
}
const char *psmtp_ui_sort_label(int mode) {
    static const char *const labels[]={"Name (A-Z)","Name (Z-A)","Size (smallest)","Size (largest)","Modified (newest)","Modified (oldest)","File type"};
    return labels[mode>=0 && mode<PSMTP_SORT_COUNT?mode:0];
}
int psmtp_ui_compare(int mode,const char *an,int af,uint64_t as,const char *ad,const char *bn,int bf,uint64_t bs,const char *bd) {
    int result=0;if(af!=bf)return bf-af;
    if(mode==PSMTP_SORT_SIZE || mode==PSMTP_SORT_SIZE_DESC){if(as==UINT64_MAX || bs==UINT64_MAX)result=as==bs?0:as==UINT64_MAX?1:-1;else {result=as<bs?-1:as>bs;if(mode==PSMTP_SORT_SIZE_DESC)result=-result;}}
    else if(mode==PSMTP_SORT_NEWEST || mode==PSMTP_SORT_OLDEST){if(!*ad || !*bd)result=!*ad && !*bd?0:!*ad?1:-1;else {result=strcmp(ad,bd);if(mode==PSMTP_SORT_NEWEST)result=-result;}}
    else if(mode==PSMTP_SORT_TYPE)result=psmtp_ui_name_compare(psmtp_ui_kind(an,af),psmtp_ui_kind(bn,bf));
    if(!result)result=psmtp_ui_name_compare(an,bn)*(mode==PSMTP_SORT_NAME_DESC?-1:1);return result;
}
const char *psmtp_ui_kind(const char *name,int folder) {
    const char *ext;
    if(folder) return "Folder";
    ext=strrchr(name,'.'); if(!ext) return "File";
    if(!psmtp_ui_name_compare(ext,".pkg")) return "Package";
    if(!psmtp_ui_name_compare(ext,".jpg") || !psmtp_ui_name_compare(ext,".png") || !psmtp_ui_name_compare(ext,".jpeg")) return "Image";
    if(!psmtp_ui_name_compare(ext,".mp4") || !psmtp_ui_name_compare(ext,".mkv") || !psmtp_ui_name_compare(ext,".mov") || !psmtp_ui_name_compare(ext,".m4v")) return "Video";
    if(!psmtp_ui_name_compare(ext,".mp3") || !psmtp_ui_name_compare(ext,".flac")) return "Audio";
    if(!psmtp_ui_name_compare(ext,".zip") || !psmtp_ui_name_compare(ext,".7z")) return "Archive";
    if(!psmtp_ui_name_compare(ext,".txt") || !psmtp_ui_name_compare(ext,".log")) return "Text";
    return "File";
}
void psmtp_ui_extension(char out[12],const char *name,int pkg) {
    const char *dot=strrchr(name,'.');size_t i,n;
    strcpy(out,pkg?"pkg":"bin");
    if(pkg || !dot || !(n=strlen(dot+1)) || n>8 || !psmtp_ui_name_compare(dot,".pkg")) return;
    for(i=0;i<n;++i) if(!((dot[1+i]>='a' && dot[1+i]<='z') || (dot[1+i]>='A' && dot[1+i]<='Z') || (dot[1+i]>='0' && dot[1+i]<='9'))) return;
    for(i=0;i<n;++i) out[i]=(char)tolower((unsigned char)dot[1+i]);out[n]=0;
}
