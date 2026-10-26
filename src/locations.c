/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#include "psmtp/locations.h"
#include <string.h>
#include <stdio.h>
int psmtp_location_same(const psmtp_location *a,const psmtp_location *b) {
    return a->kind==b->kind && a->vendor_id==b->vendor_id && a->product_id==b->product_id &&
        (a->kind==PSMTP_LOCATION_PHONE && (a->connection || b->connection)?a->connection==b->connection:a->slot==b->slot);
}
void psmtp_locations_build(psmtp_locations *l,const psmtp_ps4_candidate *phones,size_t n,unsigned usb) {
    psmtp_locations old=*l;psmtp_location active={0},selected={0};int previous=l->count!=0;size_t i,j,number=0;
    if(previous) {active=l->entries[l->active];selected=l->entries[l->selected];}
    memset(l,0,sizeof(*l));if(n>128) n=128;
    for(i=0;i<n;++i) if(phones[i].mtp) {
        psmtp_location *e=&l->entries[l->count++];e->kind=PSMTP_LOCATION_PHONE;e->slot=(unsigned)i;
        e->vendor_id=phones[i].vendor_id;e->product_id=phones[i].product_id;e->connection=phones[i].connection;
        snprintf(e->label,sizeof(e->label),"Phone %zu",++number);
        if(phones[i].name[0]) {snprintf(e->label,sizeof(e->label),"%s",phones[i].name);e->named=1;}
    }
    for(i=0;i<PSMTP_USB_SLOTS;++i) if(usb&(1u<<i)) {
        psmtp_location *e=&l->entries[l->count++];e->kind=PSMTP_LOCATION_USB;e->slot=(unsigned)i;
        snprintf(e->label,sizeof(e->label),"USB%zu",i);snprintf(e->root,sizeof(e->root),"/mnt/usb%zu",i);
    }
    {psmtp_location *e=&l->entries[l->count++];e->kind=PSMTP_LOCATION_INTERNAL;strcpy(e->label,"Internal storage");strcpy(e->root,"/");}
    if(previous) for(i=0;i<l->count;++i) {
        for(j=0;j<old.count;++j) if(psmtp_location_same(&old.entries[j],&l->entries[i])) {if(old.entries[j].named>=l->entries[i].named) {strcpy(l->entries[i].label,old.entries[j].label);l->entries[i].named=old.entries[j].named;}break;}
        if(psmtp_location_same(&active,&l->entries[i])) l->active=i;
        if(psmtp_location_same(&selected,&l->entries[i])) l->selected=i;
    }
}
int psmtp_locations_move(psmtp_locations *l,int delta) {
    size_t old=l->selected,step;if(!l->count) return 0;
    if(delta<0) {step=(size_t)(-(int64_t)delta);l->selected=step>old?0:old-step;}
    else {step=(size_t)delta;l->selected=step>l->count-1-old?l->count-1:old+step;}
    return l->selected!=old;
}
int psmtp_location_phone_index(const psmtp_location *e,const psmtp_ps4_candidate *p,size_t n) {
    size_t i;if(e->kind!=PSMTP_LOCATION_PHONE) return -1;
    if(e->connection) {
        for(i=0;i<n;++i) if(p[i].mtp && p[i].connection==e->connection && p[i].vendor_id==e->vendor_id && p[i].product_id==e->product_id) return (int)i;
        return -1;
    }
    return e->slot<n && p[e->slot].mtp && p[e->slot].vendor_id==e->vendor_id && p[e->slot].product_id==e->product_id?(int)e->slot:-1;
}
