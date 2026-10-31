/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PSMTP_UPDATE_CLIENT_H
#define PSMTP_UPDATE_CLIENT_H
#include "psmtp/update.h"
#include "work.h"
typedef struct {psmtp_update_manifest manifest;int download;char saved_path[256];} psmtp_update_job;
/* Run on the existing joinable work executor. Owner retains job until joined. */
int psmtp_update_run(void *,psmtp_work *);
#endif
