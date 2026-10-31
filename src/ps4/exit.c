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
#include "lifecycle.h"
#ifdef PSMTP_TEST_EXIT
#include "ps4_exit_test_api.h"
#else
#include <orbis/libkernel.h>
#include <orbis/Sysmodule.h>
#include <orbis/SystemService.h>
#endif
#include <inttypes.h>
#include <unistd.h>
#include <stdlib.h>

#include "psmtp/cleanup_log.h"

static FILE *(*app_cleanup)(void);
static int exiting;
int psmtp_ps4_exit_pending(void){psmtp_lifecycle_poll();return psmtp_lifecycle_requested()!=0;}
#ifndef PSMTP_TEST_EXIT
static void normal_exit(void){psmtp_ps4_exit(0,NULL);}
#endif
void psmtp_ps4_exit_set_cleanup(FILE *(*cleanup)(void)){
    app_cleanup=cleanup;
    /* Handlers only publish intent. Main-thread checkpoints unwind transfers
     * through their descriptor disposal paths before invoking cleanup. */
    /* Native handlers are installed by psmtp_lifecycle_open with verified ABI. */
#ifndef PSMTP_TEST_EXIT
    (void)atexit(normal_exit);
#endif
}

static void sync_log(FILE *log) {
    if(log) { (void)(fflush)(log); (void)fsync(fileno(log)); }
}

void psmtp_ps4_exit(int status,FILE *log) {
    uint32_t loaded;int32_t rc;
    if(__atomic_exchange_n(&exiting,1,__ATOMIC_ACQ_REL))for(;;)sceKernelUsleep(1000000);
    psmtp_lifecycle_shutdown_begin();
    psmtp_lifecycle_mark("exit-enter",psmtp_lifecycle_requested());
    if(app_cleanup)log=app_cleanup();
    psmtp_lifecycle_mark("exit-cleanup-returned",status);
    /* Legacy early failure stream, if no coordinator owned it. */
    sync_log(log);if(log && log!=stdout && log!=stderr){(void)fclose(log);log=NULL;}
    loaded=sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SYSTEM_SERVICE);
    psmtp_lifecycle_mark("system-service-load-returned",(int32_t)loaded);
    psmtp_lifecycle_mark("loadexec-enter",loaded?(int32_t)loaded:status);
    psmtp_lifecycle_close();
    /* The diagnostic descriptor is already closed. A returned request cannot
     * prove termination; preserve the known-safe quiescent fallback instead
     * of returning to libc's unsafe raw _exit or killing a borrowed stack. */
    if(!loaded){rc=sceSystemServiceLoadExec("exit",NULL);(void)rc;}

    for(;;) sceKernelUsleep(1000000);
}

/* Protect libc's final termination path too, including early startup failure.
 * This local definition prevents a dynamic import of libkernel's _exit.
 * Normal UI shutdown calls the helper directly with its still-valid log. */
#ifndef PSMTP_TEST_EXIT
_Noreturn void _exit(int status) { psmtp_ps4_exit(status,NULL); }
#endif
