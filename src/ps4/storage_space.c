/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#define _GNU_SOURCE
#include "storage_space.h"
#include "file_store.h"
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#ifdef PSMTP_TEST_FILE_SYSTEM
#include "ps4_copy_test_api.h"
#else
#include <orbis/libkernel.h>
#include <unistd.h>
#endif
/* Native FreeBSD/Orbis prefix, never the host or SDK musl statvfs ABI. */
typedef struct {uint32_t version,type;uint64_t flags,bsize,iosize,blocks,bfree;int64_t bavail;} space_prefix;
typedef char space_layout[(offsetof(space_prefix,bsize)==16 && offsetof(space_prefix,bavail)==48)?1:-1];
static int space_decode(const space_prefix *p,uint64_t *available,uint64_t *block) {
    if(p->version!=UINT32_C(0x20030518) || !p->bsize || p->bsize>UINT64_C(1073741824) ||
       !p->blocks || p->bfree>p->blocks || (p->bavail>0 && (uint64_t)p->bavail>p->bfree))return MTP_SPACE_UNKNOWN;
    if(p->bavail>0 && (uint64_t)p->bavail>UINT64_MAX/p->bsize)return MTP_SPACE_UNKNOWN;
    *block=p->bsize;*available=p->bavail>0?(uint64_t)p->bavail*p->bsize:0;return 0;
}
static int space_kernel(const char *name,size_t cap) {
    const char *leaf;size_t n=strnlen(name,cap);
    if(n==cap)return 0;leaf=strrchr(name,'/');leaf=leaf?leaf+1:name;
    return !strcmp(leaf,"libkernel") || !strcmp(leaf,"libkernel.sprx") || !strcmp(leaf,"libkernel.prx") ||
        !strcmp(leaf,"libkernel_sys") || !strcmp(leaf,"libkernel_sys.sprx") || !strcmp(leaf,"libkernel_sys.prx") ||
        !strcmp(leaf,"libkernel_web") || !strcmp(leaf,"libkernel_web.sprx") || !strcmp(leaf,"libkernel_web.prx");
}
int psmtp_native_space(const char *root,const char *path,uint64_t *available,uint64_t *block,FILE *log) {
    static const char *names[]={"_fstatfs","fstatfs"};
    OrbisKernelModule modules[128];size_t count=0,i,j;int fd=-1,rc=MTP_SPACE_UNKNOWN,native_rc=-1,query_errno=0;
    union {space_prefix prefix;uint8_t bytes[4096];} data={0};
    *available=*block=0;

    fd=psmtp_native_directory(root,path);if(fd<0){rc=MTP_FILESYSTEM;query_errno=errno;goto done;}
    if(sceKernelGetModuleList(modules,128,&count) || count>128)goto done;
    for(i=0;i<count && rc;++i){OrbisKernelModuleInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);
        if(modules[i]>INT32_MAX || sceKernelGetModuleInfo(modules[i],&info) || !space_kernel(info.name,sizeof(info.name)))continue;
        for(j=0;j<sizeof(names)/sizeof(*names) && rc;++j){
            int (*query)(int,void *)=NULL;void *symbol=NULL;
            if(sceKernelDlsym((int32_t)modules[i],names[j],&symbol) || !symbol)continue;
            memcpy(&query,&symbol,sizeof(query));memset(&data,0,sizeof(data));errno=0;
            native_rc=query(fd,&data);query_errno=errno;
            if(!native_rc)rc=space_decode(&data.prefix,available,block);
            if(log)fprintf(log,"stage=storage-query module=%u symbol=%s native-status=%d errno=%d valid=%d\n",(unsigned)modules[i],names[j],native_rc,query_errno,!rc);
        }
    }
done:
    if(fd>=0 && close(fd)){rc=MTP_FILESYSTEM;query_errno=errno;}
    if(rc)*available=*block=0;
    if(log)fprintf(log,"stage=storage-space status=%d available=%" PRIu64 " block=%" PRIu64 " method=native-fstatfs native-status=%d errno=%d version=%08" PRIx32 "\n",rc,*available,*block,native_rc,query_errno,data.prefix.version);
    return rc;
}
int psmtp_space_require(const char *root,const char *path,uint64_t required,FILE *log) {
    uint64_t available,block;int rc=psmtp_native_space(root,path,&available,&block,log),known=!rc;
    if(known && required>available)rc=MTP_NO_SPACE;



    if(rc==MTP_SPACE_UNKNOWN)rc=0;
    if(log)fprintf(log,"stage=storage-preflight status=%d known=%d required=%" PRIu64 " available=%" PRIu64 " policy=%s\n",rc,known,required,available,known?"capacity":"checked-io");
    return rc;
}
