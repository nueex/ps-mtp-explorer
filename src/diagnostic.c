/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#include "psmtp/diagnostic.h"
#include "psmtp/browser.h"
#include "psmtp/navigator.h"
#include <inttypes.h>

static void stage(FILE *out,mtp_client *c,const char *name,int rc) {
    fprintf(out,"stage=%s status=%d (%s) op=0x%04x tid=%" PRIu32 " response=0x%04x poisoned=%d\n",
            name,rc,mtp_error_string(rc),c->last_op,c->last_tid,c->response,c->poisoned);
    fflush(out);
}
int mtp_diagnose_roots(mtp_client *c,FILE *out,int names) {
    mtp_device_info di; uint32_t storages[MTP_MAX_STORAGES],objects[MTP_MAX_ITEMS];
    size_t ns=0,no=0,i,j; int e,result=0;
    const uint16_t required[]={0x1002,0x1003,0x1004,0x1005,0x1007,0x1008};
    fputs("stage=device-info begin (read-only; identity strings suppressed)\n",out); fflush(out);
    e=mtp_get_device_info(c,&di); stage(out,c,"device-info",e); if(e) return e;
    for(i=0;i<sizeof(required)/sizeof(required[0]);++i) if(!mtp_supports(&di,required[i])) {
        fprintf(out,"stage=capability status=%d missing-op=0x%04x\n",MTP_UNSUPPORTED,required[i]);
        return MTP_UNSUPPORTED;
    }
    fputs("stage=open-session begin\n",out); fflush(out);
    e=mtp_open_session(c,1); stage(out,c,"open-session",e); if(e) return e;
    fputs("stage=storage-ids begin\n",out); fflush(out);
    e=mtp_get_storage_ids(c,storages,MTP_MAX_STORAGES,&ns); stage(out,c,"storage-ids",e);
    if(e) { result=e; goto cleanup; }
    fprintf(out,"storage-count=%zu\n",ns);
    if(!ns) {
        fputs("NO STORAGE: unlock phone, select File Transfer and accept its access prompt.\n",out);
        result=MTP_UNSUPPORTED; goto cleanup;
    }
    for(i=0;i<ns && i<MTP_DIAG_STORAGES;++i) {
        mtp_storage_info si;
        fprintf(out,"stage=storage-info begin storage=%08" PRIx32 "\n",storages[i]); fflush(out);
        e=mtp_get_storage_info(c,storages[i],&si); stage(out,c,"storage-info",e);
        if(e) { result=e; goto cleanup; }
        fprintf(out,"storage=%08" PRIx32 " capacity=%" PRIu64 " free=%" PRIu64 " access=%u\n",
                storages[i],si.capacity,si.free_bytes,si.access);
        fprintf(out,"stage=root-handles begin storage=%08" PRIx32 " parent=ffffffff\n",storages[i]); fflush(out);
        e=mtp_get_handles(c,storages[i],MTP_ROOT,objects,MTP_MAX_ITEMS,&no); stage(out,c,"root-handles",e);
        if(e) { result=e; goto cleanup; }
        fprintf(out,"root-objects=%zu metadata-sample-limit=%u\n",no,MTP_DIAG_OBJECTS);
        for(j=0;j<no && j<MTP_DIAG_OBJECTS;++j) {
            mtp_object_info object;
            fprintf(out,"stage=root-object-info begin handle=%08" PRIx32 "\n",objects[j]); fflush(out);
            e=mtp_get_object_info(c,objects[j],&object); stage(out,c,"root-object-info",e);
            if(e) { result=e; goto cleanup; }
            if(object.storage!=storages[i] || (object.parent!=0 && object.parent!=MTP_ROOT)) {
                fputs("stage=root-consistency status=-4 (storage/parent mismatch)\n",out);
                result=MTP_PROTOCOL; goto cleanup;
            }
            fprintf(out,"handle=%08" PRIx32 " kind=%s format=%04x size32=%" PRIu32,
                    objects[j],mtp_is_folder(&object)?"folder":"object",object.format,object.size32);
            if(names) { fputs(" name=",out); mtp_print_name(out,object.name); }
            fputc('\n',out);
        }
        if(no>MTP_DIAG_OBJECTS) fputs("Metadata sample capped; root handle count above is complete within the 4096-handle limit.\n",out);
    }
    if(ns>MTP_DIAG_STORAGES) fputs("Storage diagnostics capped at four storages.\n",out);
cleanup:
    fputs("stage=close-session begin (skipped on unusable stream)\n",out); fflush(out);
    e=mtp_close_session(c); stage(out,c,"close-session",e);
    return result?result:e;
}
