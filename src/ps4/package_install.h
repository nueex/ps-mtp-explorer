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
#include "psmtp/install_error.h"
#include "phone_install_server.h"
typedef struct psmtp_install_session psmtp_install_session;
typedef struct {uint64_t done,total;int task,complete,source_error,background;uint32_t native_error;unsigned requests;int percent,progress_known,preparing,copy_percent;psmtp_install_failure failure;} psmtp_install_report;
int psmtp_install_prepare(psmtp_install_session **,const psmtp_op_ref *,mtp_client *,psmtp_pkg_info *,FILE *);
int psmtp_install_prepare_stream(psmtp_install_session **,const char *,const psmtp_phone_source *,
    void (*retain)(void *),void (*release)(void *),FILE *);
/* Confirmed phone copy and fast size/sample validation replace the session with a local
 * source. Only the owned staging copy is removed when the attempt ends. */
int psmtp_install_stage(psmtp_install_session **,int,mtp_copy_progress,void *);
int psmtp_install_begin(psmtp_install_session *,int);
/* Opt-in phone source; no network/staging is started until confirmed begin. */
int psmtp_install_phone_direct(psmtp_install_session *,psmtp_phone_control,void *);
/* The pointer remains live only when reader teardown could not safely drain. */
int psmtp_install_close_session(psmtp_install_session **,int,uint32_t *);
/* Transfer ownership to native BGFT after a successful StartTask. The source
 * stays available until completion; process closure never cancels handed-off tasks. */
int psmtp_install_handoff(psmtp_install_session **,psmtp_install_report *);
void psmtp_install_collect(FILE *);
/* UI reads only after the serialized maintenance worker joins. */
int psmtp_install_take_failure(psmtp_install_failure *);
void psmtp_install_get_failure(const psmtp_install_session *,int,psmtp_install_failure *);
int psmtp_install_background_pending(void);
int psmtp_install_poll(psmtp_install_session *,psmtp_install_report *);
/* Stops only this session's newly registered task, then closes source handles. */
int psmtp_install_cleanup(psmtp_install_session *);
uint32_t psmtp_install_native_error(const psmtp_install_session *);
int psmtp_install_close(psmtp_install_session *,int);
/* Also retains a native stop error before the session is freed. */
int psmtp_install_close_report(psmtp_install_session *,int,uint32_t *);
/* Nonzero: a service could not drain; retain credentials until process exit. */
void psmtp_install_stop(void);
int psmtp_install_retained(void);
int psmtp_install_phone_borrowed(void);
int psmtp_install_shutdown(FILE *);
#endif
