/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PSMTP_INSTALL_ERROR_H
#define PSMTP_INSTALL_ERROR_H
#include <stddef.h>
#include <stdint.h>
enum psmtp_install_step {PSMTP_INSTALL_INSPECT,PSMTP_INSTALL_STAGE_COPY,PSMTP_INSTALL_SOURCE_CHECK,
    PSMTP_INSTALL_APP_INIT,PSMTP_INSTALL_BGFT_INIT,PSMTP_INSTALL_PATCH_BASE,
    PSMTP_INSTALL_REGISTER,PSMTP_INSTALL_START,PSMTP_INSTALL_PROGRESS_QUERY,PSMTP_INSTALL_PROGRESS_RESULT,PSMTP_INSTALL_CLEANUP,
    PSMTP_INSTALL_PHONE_TRANSFER};
enum psmtp_install_error_kind {PSMTP_INSTALL_UNKNOWN,PSMTP_INSTALL_INTEGRITY,PSMTP_INSTALL_READ_ERROR,
    PSMTP_INSTALL_SPACE_ERROR,PSMTP_INSTALL_PATCH_ERROR,PSMTP_INSTALL_EXISTING_TASK,PSMTP_INSTALL_ALREADY_INSTALLED};
typedef struct {uint32_t native_error;int app_error,step,patch,base_missing,task;char title_id[10];} psmtp_install_failure;
enum psmtp_install_error_kind psmtp_install_error_kind(uint32_t);
const char *psmtp_install_error_name(uint32_t);
const char *psmtp_install_step_name(int);
const char *psmtp_install_failure_title(const psmtp_install_failure *);
void psmtp_install_failure_text(const psmtp_install_failure *,char *,size_t);
#endif
