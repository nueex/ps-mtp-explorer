/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */




#include "process_access.h"
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#ifdef PSMTP_TEST_PROCESS_ACCESS
#include "ps4_access_test_api.h"
#else
#include <orbis/libkernel.h>
#include <unistd.h>
#endif
#include "../../third_party/libjbc/libjbc.h"
typedef char access_credential_layout[(sizeof(jbc_cred)==80 && offsetof(jbc_cred,prison)==24 && offsetof(jbc_cred,sceProcType)==56)?1:-1];
static int kernel_pointer(uintptr_t value) {
    return value>=UINT64_C(0xffff800000000000) && value<UINT64_C(0xfffffffffffff000) && !(value&7);
}
static int same_identity(const jbc_cred *a,const jbc_cred *b) {
    return a->uid==b->uid && a->ruid==b->ruid && a->svuid==b->svuid &&
        a->rgid==b->rgid && a->svgid==b->svgid && a->sceProcType==b->sceProcType &&
        a->sonyCred==b->sonyCred && a->sceProcCap==b->sceProcCap;
}
int psmtp_process_access(FILE *out) {
    OrbisKernelSwVersion version={0};jbc_cred before,global,after;uintptr_t td,proc;int sandbox,rc;
    sandbox=sceKernelIsInSandbox();version.Size=sizeof(version);
    rc=sceKernelGetSystemSwVersion(&version);
    fprintf(out,"stage=filesystem-access begin sandbox=%d uid=%u firmware-rc=%d firmware=%08" PRIx32 "\n",sandbox,(unsigned)getuid(),rc,version.Version);




    if(sandbox!=0 && sandbox!=1) {
        fputs("stage=filesystem-access unavailable reason=sandbox-query changed=0\n",out);return 1;
    }


    td=jbc_krw_get_td();
    if(!kernel_pointer(td)) {fputs("stage=filesystem-access unavailable reason=kernel-gate changed=0\n",out);return 1;}
    proc=jbc_krw_read64(td+8,KERNEL_HEAP);
    if(!kernel_pointer(proc) || (uint32_t)jbc_krw_read64(proc+0xb0,KERNEL_HEAP)!=(uint32_t)getpid()) {
        fputs("stage=filesystem-access unavailable reason=process-layout changed=0\n",out);return 1;
    }
    memset(&before,0,sizeof(before));
    if(!kernel_pointer(jbc_krw_read64(proc+0x40,KERNEL_HEAP)) ||
       !kernel_pointer(jbc_krw_read64(proc+0x48,KERNEL_HEAP))) {
        fputs("stage=filesystem-access unavailable reason=context-layout changed=0\n",out);return 1;
    }
    if(jbc_get_cred(&before) || !kernel_pointer(before.prison) || !kernel_pointer(before.rdir) ||
       !kernel_pointer(before.cdir) || !kernel_pointer(before.jdir) || before.uid!=(uint32_t)getuid()) {
        fputs("stage=filesystem-access unavailable reason=credential-snapshot changed=0\n",out);return 1;
    }
    global=before;
    if(jbc_jailbreak_cred(&global) || !kernel_pointer(global.prison) || !kernel_pointer(global.rdir)) {
        fputs("stage=filesystem-access unavailable reason=global-root changed=0\n",out);return 1;
    }
    /* 0.11.1 changed the Unix identity too. On this console UID 0 cannot
     * create inside existing UID 1 /data/psmtp directories. Keep the original
     * Unix owner/group, but retain libjbc's Sony type/credentials/capabilities.
     * The older namespace-only profile restored those Sony fields as well,
     * leaving capacity queries denied. Never toggle credentials in workers. */
    global.uid=before.uid;global.ruid=before.ruid;global.svuid=before.svuid;
    global.rgid=before.rgid;global.svgid=before.svgid;
    fputs("stage=filesystem-access apply method=libjbc scope=current-process profile=owner-and-capacity uid-preserved=1\n",out);fflush(out);
    rc=jbc_set_cred(&global);
    memset(&after,0,sizeof(after));
    if(rc || jbc_get_cred(&after) || !same_identity(&global,&after) ||
       after.prison!=global.prison || after.rdir!=global.rdir || after.cdir!=global.cdir ||
       after.jdir!=global.jdir || sceKernelIsInSandbox()!=0) {
        fprintf(out,"stage=filesystem-access failed status=%d verified=0; browsing stopped\n",rc);return -1;
    }
    fprintf(out,"stage=filesystem-access complete verified=1 sandbox=0 uid=%u profile=owner-and-capacity uid-preserved=1\n",(unsigned)getuid());
    return 0;
}
