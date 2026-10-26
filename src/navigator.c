/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#include "psmtp/navigator.h"
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include "psmtp/ui_format.h"

int mtp_is_folder(const mtp_object_info *info) {


    return info->format==0x3001 && info->association<=1;
}
static int stage(mtp_navigator *nav,const char *name,int rc) {
    mtp_client *c=nav->client;
    fprintf(nav->log,"stage=browser-%s status=%d op=%04x tid=%" PRIu32 " response=%04x poisoned=%d\n",
            name,rc,c->last_op,c->last_tid,c->response,c->poisoned);
    fflush(nav->log); return rc;
}
static int page(mtp_navigator *nav) {
    size_t i,end; int rc;
    nav->page_start=(nav->selected/MTP_NAV_PAGE)*MTP_NAV_PAGE; nav->page_count=0;
    end=nav->count-nav->page_start; if(end>MTP_NAV_PAGE) end=MTP_NAV_PAGE;
    for(i=0;i<end;++i) {
        mtp_nav_entry *entry=&nav->entries[i];
        entry->handle=nav->handles[nav->page_start+i];
        fprintf(nav->log,"stage=browser-object-info begin handle=%08" PRIx32 "\n",entry->handle); fflush(nav->log);
        if(nav->catalog) *entry=nav->catalog[nav->page_start+i];
        else {rc=mtp_get_object_info(nav->client,entry->handle,&entry->info);
            if(stage(nav,"object-info",rc)) return rc;}
        if(entry->info.storage!=nav->storages[nav->storage_index] ||
           (nav->parent==MTP_ROOT && entry->info.parent!=0 && entry->info.parent!=MTP_ROOT) ||
           (nav->parent!=MTP_ROOT && entry->info.parent!=nav->parent))
            return stage(nav,"parent-check",MTP_PROTOCOL);
        if(!nav->catalog || !entry->exact_size){entry->bytes=entry->info.size32;entry->exact_size=entry->info.size32!=UINT32_MAX;}
        if(!mtp_is_folder(&entry->info) && !entry->exact_size && nav->size64_supported) {
            uint64_t bytes=0;rc=mtp_get_object_size(nav->client,entry->handle,&bytes);
            stage(nav,"file-size",rc);
            if(!rc) {entry->bytes=bytes;entry->exact_size=1;}
            else if(nav->client->poisoned) return rc;
        }
        fprintf(nav->log,"browser-item handle=%08" PRIx32 " parent=%08" PRIx32 " kind=%s format=%04x association=%u size32=%" PRIu32 "\n",
                entry->handle,entry->info.parent,mtp_is_folder(&entry->info)?"folder":"file",
                entry->info.format,entry->info.association,entry->info.size32);
        if(nav->catalog)nav->catalog[nav->page_start+i]=*entry;
        ++nav->page_count;
    }
    fprintf(nav->log,"browser-page first=%zu displayed=%zu total=%zu names-in-log=0\n",nav->page_start,nav->page_count,nav->count);
    return 0;
}
static int nav_sort_mode;
static int nav_compare(const void *a,const void *b) {
    const mtp_nav_entry *x=a,*y=b;int name=psmtp_ui_compare(nav_sort_mode,x->info.name,mtp_is_folder(&x->info),x->exact_size?x->bytes:UINT64_MAX,x->info.modified,y->info.name,mtp_is_folder(&y->info),y->exact_size?y->bytes:UINT64_MAX,y->info.modified);
    return name?name:x->handle<y->handle?-1:x->handle!=y->handle;
}
static int load(mtp_navigator *nav,uint32_t parent,size_t selected) {
    size_t i,j; int rc;
    if(!parent) return MTP_STATE;
    if(parent!=MTP_ROOT) {
        mtp_object_info folder;
        fprintf(nav->log,"stage=browser-folder-info begin handle=%08" PRIx32 "\n",parent); fflush(nav->log);
        rc=mtp_get_object_info(nav->client,parent,&folder);
        if(stage(nav,"folder-info",rc)) return rc;
        if(folder.storage!=nav->storages[nav->storage_index] || !mtp_is_folder(&folder))
            return stage(nav,"folder-check",MTP_PROTOCOL);
        memcpy(nav->folder_name,folder.name,sizeof(nav->folder_name));
    } else strcpy(nav->folder_name,"/");
    free(nav->catalog);nav->catalog=NULL;nav->count=0; nav->page_count=0;
    fprintf(nav->log,"stage=browser-handles begin storage=%08" PRIx32 " parent=%08" PRIx32 "\n",nav->storages[nav->storage_index],parent);
    fflush(nav->log);
    rc=mtp_get_handles(nav->client,nav->storages[nav->storage_index],parent,nav->handles,MTP_MAX_ITEMS,&nav->count);
    if(stage(nav,"handles",rc)) return rc;


    for(i=0;i<nav->count;++i) {
        if(!nav->handles[i] || nav->handles[i]==MTP_ROOT) return stage(nav,"handle-check",MTP_PROTOCOL);
        for(j=0;j<i;++j) if(nav->handles[i]==nav->handles[j]) return stage(nav,"duplicate-handle",MTP_PROTOCOL);
    }
    if(nav->scan_progress && nav->count) {
        nav->catalog=calloc(nav->count,sizeof(*nav->catalog));if(!nav->catalog) return MTP_MEMORY;
        for(i=0;i<nav->count;++i) {
            mtp_nav_entry *e=&nav->catalog[i];
            if(nav->scan_progress(nav->scan_user,i,nav->count)) return MTP_CANCELLED;
            e->handle=nav->handles[i];rc=mtp_get_object_info(nav->client,e->handle,&e->info);
            e->bytes=e->info.size32;e->exact_size=e->info.size32!=UINT32_MAX;
            if(stage(nav,"catalog-object",rc)) return rc;
            if(e->info.storage!=nav->storages[nav->storage_index] ||
               (parent==MTP_ROOT?e->info.parent!=0 && e->info.parent!=MTP_ROOT:e->info.parent!=parent)) return stage(nav,"catalog-parent",MTP_PROTOCOL);
        }
        nav_sort_mode=nav->sort_mode;qsort(nav->catalog,nav->count,sizeof(*nav->catalog),nav_compare);
        for(i=0;i<nav->count;++i) nav->handles[i]=nav->catalog[i].handle;
        if(nav->scan_progress(nav->scan_user,nav->count,nav->count)) return MTP_CANCELLED;
    }
    nav->parent=parent;
    nav->selected=nav->count?(selected<nav->count?selected:nav->count-1):0;
    return page(nav);
}
int mtp_nav_storage(mtp_navigator *nav,size_t index) {
    int rc;
    if(index>=nav->storage_count) return MTP_STATE;
    fprintf(nav->log,"stage=browser-storage-info begin storage=%08" PRIx32 "\n",nav->storages[index]); fflush(nav->log);
    rc=mtp_get_storage_info(nav->client,nav->storages[index],&nav->storage);
    if(stage(nav,"storage-info",rc)) return rc;
    nav->storage_index=index; nav->depth=0;
    return load(nav,MTP_ROOT,0);
}
int mtp_nav_open(mtp_navigator *nav,mtp_client *client,FILE *out) {return mtp_nav_open_ex(nav,client,out,NULL,NULL);}
int mtp_nav_open_ex(mtp_navigator *nav,mtp_client *client,FILE *out,int (*scan)(void *,size_t,size_t),void *user) {
    mtp_device_info device; size_t i; int rc;
    const uint16_t ops[]={0x1002,0x1003,0x1004,0x1005,0x1007,0x1008};
    memset(nav,0,sizeof(*nav)); nav->client=client; nav->log=out;nav->scan_progress=scan;nav->scan_user=user;
    fputs("stage=browser-device-info begin; metadata only, names never logged\n",out); fflush(out);
    rc=mtp_get_device_info(client,&device); if(stage(nav,"device-info",rc)) return rc;
    for(i=0;i<sizeof(ops)/sizeof(ops[0]);++i) if(!mtp_supports(&device,ops[i])) return stage(nav,"capability",MTP_UNSUPPORTED);
    snprintf(nav->device_name,sizeof(nav->device_name),"%.95s",device.model[0]?device.model:"Connected phone");
    nav->identity_valid=device.identity_valid;memcpy(nav->device_identity,device.identity,32);
    nav->size64_supported=mtp_supports(&device,0x9803);
    nav->rename_supported=mtp_supports(&device,0x9802) && mtp_supports(&device,0x9804);
    nav->thumbnail_supported=mtp_supports(&device,0x100a);nav->partial_supported=mtp_supports(&device,0x101b);nav->partial64_supported=mtp_supports(&device,0x95c1);
    fputs("stage=browser-open-session begin\n",out); fflush(out);
    rc=mtp_open_session(client,1); if(stage(nav,"open-session",rc)) return rc;
    fprintf(out,"stage=browser-session fresh=1 recovered-stale=%d\n",client->session_recovered);
    fputs("stage=browser-storage-ids begin\n",out); fflush(out);
    rc=mtp_get_storage_ids(client,nav->storages,MTP_MAX_STORAGES,&nav->storage_count);
    if(stage(nav,"storage-ids",rc)) return rc;
    if(!nav->storage_count) return stage(nav,"no-storage",MTP_UNSUPPORTED);
    for(i=0;i<nav->storage_count;++i) if(!nav->storages[i] || nav->storages[i]==MTP_ROOT) return stage(nav,"storage-check",MTP_PROTOCOL);
    return mtp_nav_storage(nav,0);
}
const mtp_nav_entry *mtp_nav_selected(const mtp_navigator *nav) {
    if(!nav->count || nav->selected<nav->page_start || nav->selected-nav->page_start>=nav->page_count) return NULL;
    return &nav->entries[nav->selected-nav->page_start];
}
int mtp_nav_move(mtp_navigator *nav,int delta) {
    size_t selected=nav->selected,step;
    if(!nav->count) return 0;
    if(delta<0) { step=(size_t)(-(int64_t)delta); selected=step>selected?0:selected-step; }
    else { step=(size_t)delta; selected=step>nav->count-1-selected?nav->count-1:selected+step; }
    if(selected==nav->selected) return 0;
    nav->selected=selected;
    if(selected<nav->page_start || selected-nav->page_start>=nav->page_count) return page(nav);
    return 0;
}
int mtp_nav_enter(mtp_navigator *nav) {
    const mtp_nav_entry *entry=mtp_nav_selected(nav); size_t i; uint32_t handle; int rc;
    mtp_nav_position saved;
    if(!entry || !mtp_is_folder(&entry->info)) return MTP_STATE;
    if(nav->depth==MTP_NAV_DEPTH) return MTP_LIMIT;
    handle=entry->handle;
    if(handle==nav->parent) return stage(nav,"cycle-check",MTP_PROTOCOL);
    for(i=0;i<nav->depth;++i) if(nav->history[i].parent==handle) return stage(nav,"cycle-check",MTP_PROTOCOL);
    saved.parent=nav->parent; saved.selected=nav->selected;
    snprintf(saved.folder,sizeof(saved.folder),"%s",nav->folder_name);
    fprintf(nav->log,"browser-enter handle=%08" PRIx32 " depth=%zu\n",handle,nav->depth+1);
    rc=load(nav,handle,0);
    if(!rc) nav->history[nav->depth++]=saved;
    return rc;
}
int mtp_nav_back(mtp_navigator *nav) {
    mtp_nav_position previous; int rc;
    if(!nav->depth) return MTP_STATE;
    previous=nav->history[nav->depth-1];
    rc=load(nav,previous.parent,previous.selected); if(!rc) --nav->depth;
    return rc;
}
int mtp_nav_refresh(mtp_navigator *nav) {
    uint32_t selected=nav->count?nav->handles[nav->selected]:0;size_t i;int rc=load(nav,nav->parent,nav->selected);
    if(!rc && selected)for(i=0;i<nav->count;++i)if(nav->handles[i]==selected){nav->selected=i;return page(nav);}
    return rc;
}
int mtp_nav_sort(mtp_navigator *nav,int mode) {
    size_t i;uint32_t selected=nav->count?nav->handles[nav->selected]:0;
    if(mode<0 || mode>=PSMTP_SORT_COUNT)return MTP_STATE;
    nav->sort_mode=mode;if(!nav->catalog)return mode==0?0:MTP_UNSUPPORTED;
    nav_sort_mode=mode;qsort(nav->catalog,nav->count,sizeof(*nav->catalog),nav_compare);
    for(i=0;i<nav->count;++i){nav->handles[i]=nav->catalog[i].handle;if(nav->handles[i]==selected)nav->selected=i;}
    return page(nav);
}
int mtp_nav_close(mtp_navigator *nav) {
    free(nav->catalog);nav->catalog=NULL;
    fputs("stage=browser-close-session begin (no command if stream unusable)\n",nav->log); fflush(nav->log);
    return stage(nav,"close-session",mtp_close_session(nav->client));
}
