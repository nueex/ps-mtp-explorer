/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#include "psmtp/transfer_meter.h"
#include <stdio.h>
#include <string.h>
void psmtp_transfer_measure(psmtp_transfer_meter *m,int phase,uint64_t done,uint64_t total,uint64_t now){
    unsigned i;uint64_t elapsed,bytes;double seconds;
    if(done>total)done=total;
    if(!m->active || phase!=m->phase || total!=m->total || done<m->last_done || now<m->last_time){
        memset(m,0,sizeof(*m));m->active=1;m->phase=phase;m->total=total;m->last_change=now;
        m->samples[0].time=now;m->samples[0].bytes=done;m->count=1;
    }
    if(done!=m->last_done)m->last_change=now;
    m->last_done=done;m->last_time=now;m->eta_known=0;m->remaining_seconds=0;

    while(m->count>1 && now-m->samples[1].time>=4000){memmove(m->samples,m->samples+1,(--m->count)*sizeof(*m->samples));}
    elapsed=now-m->samples[0].time;bytes=done-m->samples[0].bytes;
    m->ready=elapsed>=1000;m->bytes_per_second=m->ready?(double)bytes*1000.0/(double)elapsed:0;
    if(now-m->last_change>=2000)m->bytes_per_second=0;
    if(m->ready && m->bytes_per_second>0 && done<total){
        seconds=(double)(total-done)/m->bytes_per_second;

        if(seconds<360000000.0){m->remaining_seconds=(uint64_t)seconds;if((double)m->remaining_seconds<seconds)++m->remaining_seconds;m->eta_known=1;}
    }
    i=m->count-1;
    if(now-m->samples[i].time>=250){if(m->count==24){memmove(m->samples,m->samples+1,23*sizeof(*m->samples));--m->count;}m->samples[m->count].time=now;m->samples[m->count].bytes=done;++m->count;}
}
void psmtp_transfer_text(const psmtp_transfer_meter *m,char *out,unsigned capacity){
    char eta[48];uint64_t s=m->remaining_seconds;
    if(!m->ready){snprintf(out,capacity,"Calculating speed...");return;}
    if(m->last_done>=m->total)snprintf(eta,sizeof(eta),"Transfer complete");
    else if(!m->eta_known)snprintf(eta,sizeof(eta),m->bytes_per_second>0?"Estimating time...":"Waiting for data...");
    else if(s>=3600)snprintf(eta,sizeof(eta),"%lluh %02llum to transfer",(unsigned long long)(s/3600),(unsigned long long)(s/60%60));
    else if(s>=60)snprintf(eta,sizeof(eta),"%llum %02llus to transfer",(unsigned long long)(s/60),(unsigned long long)(s%60));
    else snprintf(eta,sizeof(eta),"%llus to transfer",(unsigned long long)s);
    snprintf(out,capacity,"%.1f MB/s  -  %s",m->bytes_per_second/1000000.0,eta);
}
