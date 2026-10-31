/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE.
 * WITHOUT ANY WARRANTY; see LICENSE.
 */
#ifndef PSMTP_WORK_H
#define PSMTP_WORK_H
#include "psmtp/copy.h"
#include "package_install.h"
typedef struct psmtp_work psmtp_work;
typedef struct {int phase,install;uint64_t done,total;psmtp_install_report report;int cancel_requested,pause_requested,shutdown_requested,paused,error;} psmtp_work_progress_state;
typedef int (*psmtp_work_fn)(void *,psmtp_work *);
psmtp_work *psmtp_work_create(void);
FILE *psmtp_work_log(psmtp_work *);
int psmtp_work_start(psmtp_work *,psmtp_work_fn,void *,mtp_client *);
int psmtp_work_progress(void *,enum mtp_copy_phase,uint64_t,uint64_t);
void psmtp_work_install_progress(psmtp_work *,const psmtp_install_report *);
int psmtp_work_snapshot(psmtp_work *,psmtp_work_progress_state *);
int psmtp_work_cancelled(psmtp_work *);
int psmtp_work_source_state(void *,int *,unsigned *);
void psmtp_work_cancel(psmtp_work *);
void psmtp_work_shutdown(psmtp_work *);
void psmtp_work_system(psmtp_work *,int,unsigned,int);
int psmtp_work_pause_requested(psmtp_work *);
int psmtp_work_wait(psmtp_work *,unsigned);
int psmtp_work_done(psmtp_work *,int *);
size_t psmtp_work_read_log(psmtp_work *,char *,size_t);
unsigned psmtp_work_dropped_logs(psmtp_work *);
int psmtp_work_join(psmtp_work *,unsigned);
int psmtp_work_destroy(psmtp_work *);
#endif
