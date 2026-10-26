/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#include "exit.h"
#include <orbis/libkernel.h>
#include <orbis/Sysmodule.h>
#include <orbis/SystemService.h>
#include <inttypes.h>
#include <unistd.h>

static void sync_log(FILE *log) {
    if(log) { (void)fflush(log); (void)fsync(fileno(log)); }
}

void psmtp_ps4_exit(int status,FILE *log) {
    uint32_t loaded; int32_t rc;
    if(log) fprintf(log,"stage=app-exit begin status=%d method=system-service\n",status);
    sync_log(log);


    loaded=sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE);
    if(log) fprintf(log,"stage=exit-system-service-load rc=0x%08" PRIx32 "\n",loaded);
    if(!loaded) {
        if(log) fputs("stage=app-exit-request begin target=exit\n",log);
        sync_log(log);
        rc=sceSystemServiceLoadExec("exit",NULL);
        if(log) fprintf(log,"stage=app-exit-request returned=1 rc=0x%08" PRIx32 "\n",(uint32_t)rc);
    }
    if(log) fputs("stage=app-exit-wait; use PS button / Close Application if still open; no syscall fallback\n",log);
    sync_log(log);
    /* A returned zero can be an asynchronous request, not completed exit.
     * Never return to main/libc and never retry or invoke the known-bad _exit. */
    for(;;) sceKernelUsleep(1000000);
}




_Noreturn void _exit(int status) { psmtp_ps4_exit(status,NULL); }
