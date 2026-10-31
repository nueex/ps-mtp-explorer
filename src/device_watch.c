/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#include "psmtp/device_watch.h"
#include <string.h>
static int same(const psmtp_device_record *a,const psmtp_device_record *b) {
    return a->bus==b->bus && a->address==b->address && a->vendor==b->vendor && a->product==b->product && a->signature==b->signature;
}
int psmtp_device_watch_update(psmtp_device_watch *w,const psmtp_device_record *records,size_t count,uint64_t now,int force,int (*probe)(void *,size_t),void *user,psmtp_ps4_candidate *out) {
    psmtp_device_watch next={0};size_t i,j;int budget=force?128:1;
    if(count>PSMTP_WATCH_MAX || (!records && count) || !out || !probe) return MTP_STATE;
    next.next_connection=w->next_connection;next.count=count;next.next_probe=count?w->next_probe%count:0;
    for(i=0;i<count;++i) {
        if(!records[i].address) return MTP_PROTOCOL;
        for(j=0;j<i;++j) if(records[i].bus==records[j].bus && records[i].address==records[j].address) return MTP_PROTOCOL;
        next.entries[i].record=records[i];
        for(j=0;j<w->count;++j) if(same(&records[i],&w->entries[j].record)) {next.entries[i]=w->entries[j];break;}
        if(!next.entries[i].connection) {
            if(next.next_connection==UINT64_MAX) return MTP_LIMIT;
            next.entries[i].connection=++next.next_connection;
        }
    }

    for(j=0;j<count;++j) {
        size_t index=force?j:(w->next_probe+j)%count;
        psmtp_watched_device *e=&next.entries[index];
        if(budget && (force || now>=e->retry_at)) {
            int rc=probe(user,index);uint64_t delay=rc<0?500:rc==1?10000:2000;--budget;next.next_probe=(index+1)%count;
            if(rc>=0) {
                int mtp=rc==1;
                if(e->mtp!=mtp && (e->ready || e->mtp)){
                    if(next.next_connection==UINT64_MAX)return MTP_LIMIT;
                    e->connection=++next.next_connection;e->name[0]=0;e->name_attempts=0;e->name_retry_at=0;
                }
                e->ready=1;e->mtp=mtp;
            }else e->ready=0;
            e->retry_at=now>UINT64_MAX-delay?UINT64_MAX:now+delay;
        }
    }
    for(i=0;i<count;++i){psmtp_watched_device *e=&next.entries[i];
        memset(&out[i],0,sizeof(out[i]));out[i].vendor_id=e->record.vendor;out[i].product_id=e->record.product;
        out[i].mtp=(uint8_t)e->mtp;out[i].connection=e->connection;
        memcpy(out[i].name,e->name,sizeof(out[i].name));
    }
    *w=next;return 0;
}
static int present(const psmtp_ps4_candidate *p,const psmtp_ps4_candidate *list,size_t n) {
    size_t i;for(i=0;i<n;++i) if(list[i].mtp && list[i].connection==p->connection && list[i].vendor_id==p->vendor_id && list[i].product_id==p->product_id) return 1;
    return 0;
}
unsigned psmtp_device_changes(const psmtp_ps4_candidate *old,size_t old_n,unsigned old_usb,const psmtp_ps4_candidate *next,size_t next_n,unsigned next_usb) {
    size_t i;unsigned events=0;
    if(next_usb&~old_usb) events|=PSMTP_DEVICE_ADDED;
    if(old_usb&~next_usb) events|=PSMTP_DEVICE_REMOVED;
    for(i=0;i<old_n;++i) if(old[i].mtp && !present(&old[i],next,next_n)) events|=PSMTP_DEVICE_REMOVED;
    for(i=0;i<next_n;++i) if(next[i].mtp && !present(&next[i],old,old_n)) events|=PSMTP_DEVICE_ADDED;
    return events;
}
