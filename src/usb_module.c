/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#include "psmtp/usb_module.h"
#include <inttypes.h>

int psmtp_usb_module_acquire(psmtp_usb_module *m,const psmtp_usb_module_ops *ops,FILE *out) {
    int32_t rc;
    if(!m || !ops || !out || !ops->is_loaded || !ops->load || !ops->find_handle || !ops->unload) return 1;
    m->handle=-1; m->owns_reference=0;
    fputs("stage=load-usbd method=public-sysmodule id=0x00b7 begin\n",out); fflush(out);
    rc=ops->is_loaded(ops->user);
    fprintf(out,"stage=usbd-is-loaded rc=0x%08" PRIx32 "\n",(uint32_t)rc);
    if(rc) {
        fputs("stage=usbd-sysmodule-load begin\n",out); fflush(out);
        rc=ops->load(ops->user);
        fprintf(out,"stage=usbd-sysmodule-load rc=0x%08" PRIx32 "\n",(uint32_t)rc);
        if(rc) return 1;
        m->owns_reference=1;
    } else fputs("stage=usbd-reference borrowed=1; pre-existing module will not be unloaded\n",out);
    rc=ops->find_handle(ops->user,&m->handle);
    fprintf(out,"stage=load-usbd-find rc=0x%08" PRIx32 " handle=%" PRId32 " owned=%d\n",(uint32_t)rc,m->handle,m->owns_reference);
    if(rc || m->handle<0) { m->handle=-1; return 1; }
    return 0;
}
int psmtp_usb_module_release(psmtp_usb_module *m,const psmtp_usb_module_ops *ops,FILE *out) {
    int32_t rc;
    if(!m || !ops || !ops->unload || !out) return 1;
    m->handle=-1;
    if(!m->owns_reference) return 0;
    fputs("stage=unload-usbd method=public-sysmodule owned=1 begin\n",out); fflush(out);
    rc=ops->unload(ops->user);
    fprintf(out,"stage=unload-usbd rc=0x%08" PRIx32 "\n",(uint32_t)rc);
    if(rc) return 1;
    m->owns_reference=0;
    return 0;
}
