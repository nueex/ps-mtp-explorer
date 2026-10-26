/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#ifndef PSMTP_PACKAGE_INSTALL_H
#define PSMTP_PACKAGE_INSTALL_H
#include "file_ops.h"
#include "psmtp/pkg.h"
typedef struct psmtp_install_session psmtp_install_session;
typedef struct {uint64_t done,total;int task,complete,source_error;uint32_t native_error;unsigned requests;int percent,progress_known,preparing;} psmtp_install_report;
int psmtp_install_prepare(psmtp_install_session **,const psmtp_op_ref *,mtp_client *,psmtp_pkg_info *,FILE *);


int psmtp_install_stage(psmtp_install_session **,int,mtp_copy_progress,void *);
int psmtp_install_begin(psmtp_install_session *,int);
int psmtp_install_poll(psmtp_install_session *,psmtp_install_report *);

int psmtp_install_cleanup(psmtp_install_session *);
uint32_t psmtp_install_native_error(const psmtp_install_session *);
int psmtp_install_close(psmtp_install_session *,int);

int psmtp_install_close_report(psmtp_install_session *,int,uint32_t *);
void psmtp_install_shutdown(FILE *);
#endif
