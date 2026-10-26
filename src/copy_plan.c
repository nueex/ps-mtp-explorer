/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#include "psmtp/copy_plan.h"
#include "psmtp/navigator.h"
#include <stdlib.h>
#include <string.h>
static int append(mtp_copy_plan *p,mtp_client *c,uint32_t handle,const mtp_object_info *info,size_t parent,size_t depth,mtp_plan_progress progress,void *user,FILE *out) {
    size_t at=p->count,i,n; mtp_plan_item *item; uint32_t *handles; int rc;
    if(progress && progress(user,p->files,p->folders)) return MTP_CANCELLED;
    if(depth>MTP_PLAN_DEPTH || p->count==MTP_PLAN_ITEMS) return MTP_LIMIT;
    if(!handle || handle==MTP_ROOT) return MTP_PROTOCOL;
    for(i=0;i<p->count;++i) if(p->items[i].source.handle==handle) return MTP_PROTOCOL;
    item=&p->items[p->count++]; item->parent=parent; item->depth=depth; item->folder=mtp_is_folder(info);
    item->source.handle=handle; item->source.info=*info;
    if(!item->folder) {
        rc=mtp_copy_probe(c,handle,info,&item->source,out); if(rc) return rc;
        if(UINT64_MAX-p->bytes<item->source.size) return MTP_LIMIT;
        p->bytes+=item->source.size; ++p->files; return 0;
    }
    ++p->folders;
    handles=malloc(MTP_MAX_ITEMS*sizeof(*handles)); if(!handles) return MTP_MEMORY;
    rc=mtp_get_handles(c,info->storage,handle,handles,MTP_MAX_ITEMS,&n);
    if(rc) { free(handles); return rc; }
    if(n>MTP_PLAN_ITEMS-p->count) { free(handles); return MTP_LIMIT; }
    for(i=0;i<n;++i) {
        mtp_object_info child;
        if(progress && progress(user,p->files,p->folders)) { rc=MTP_CANCELLED; break; }
        rc=mtp_get_object_info(c,handles[i],&child); if(rc) break;
        if(child.storage!=info->storage || child.parent!=handle) { rc=MTP_PROTOCOL; break; }
        rc=append(p,c,handles[i],&child,at,depth+1,progress,user,out); if(rc) break;
    }
    free(handles); return rc;
}
int mtp_copy_plan_build(mtp_copy_plan *p,mtp_client *c,uint32_t handle,const mtp_object_info *info,mtp_plan_progress progress,void *user,FILE *out) {
    memset(p,0,sizeof(*p));
    return append(p,c,handle,info,SIZE_MAX,0,progress,user,out);
}
