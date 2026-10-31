/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#include "psmtp/shutdown.h"
static int quiet;
int psmtp_shutdown_logging_quiet(void){return __atomic_load_n(&quiet,__ATOMIC_ACQUIRE);}
void psmtp_shutdown_quiet_begin(void){__atomic_store_n(&quiet,1,__ATOMIC_RELEASE);}
int psmtp_shutdown_run(psmtp_shutdown *s,psmtp_shutdown_action action,void *user){
    int expected=0;unsigned i;
    if(!s || !action)return -1;
    if(!__atomic_compare_exchange_n(&s->state,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))
        return expected==2?s->first_error:-1;
    psmtp_shutdown_quiet_begin();
    for(i=0;i<PSMTP_SHUTDOWN_STEPS;++i){int rc;s->attempted|=1u<<i;rc=action(user,(enum psmtp_shutdown_step)i,s);
        s->result[i]=rc;if(rc && !s->first_error)s->first_error=rc;s->finished|=1u<<i;}
    __atomic_store_n(&s->state,2,__ATOMIC_RELEASE);return s->first_error;
}
