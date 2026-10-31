/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PSMTP_OPTIONAL_MODULE_H
#define PSMTP_OPTIONAL_MODULE_H
#include <orbis/libkernel.h>
#include <orbis/Sysmodule.h>
#include <string.h>
#include "psmtp/mtp.h"
/* Dynamic APIs keep optional online features out of mandatory imports. */
static int psmtp_optional_module(const char *wanted){
    OrbisKernelModule ids[128];size_t count=0;int found=-1;
    if(sceKernelGetModuleList(ids,128,&count) || count>128)return -1;
    for(size_t i=0;i<count;++i){OrbisKernelModuleInfo info;const char *leaf;size_t n,w=strlen(wanted);
        if(ids[i]>INT32_MAX)return -1;memset(&info,0,sizeof(info));info.size=sizeof(info);if(sceKernelGetModuleInfo(ids[i],&info))return -1;
        for(n=0;n<sizeof(info.name) && info.name[n];++n){}if(n==sizeof(info.name))return -1;leaf=strrchr(info.name,'/');leaf=leaf?leaf+1:info.name;
        if(!strncmp(leaf,wanted,w) && (!leaf[w] || !strcmp(leaf+w,".sprx") || !strcmp(leaf+w,".prx"))){if(found>=0)return -1;found=(int)ids[i];}
    }return found;
}
static int psmtp_optional_symbol(int module,const char *name,void *target,size_t size){
    void *address=NULL;if(module<0 || size!=sizeof(address) || sceKernelDlsym(module,name,&address) || !address)return MTP_UNSUPPORTED;
    memcpy(target,&address,size);return 0;
}
#endif
