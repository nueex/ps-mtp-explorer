/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PSMTP_UPDATE_CLIENT_H
#define PSMTP_UPDATE_CLIENT_H
#include "psmtp/update.h"
#include "work.h"
#include "psmtp/direct_download.h"
typedef struct {psmtp_update_manifest manifest;int download;char saved_path[256];} psmtp_update_job;
/* Run on the existing joinable work executor. Owner retains job until joined. */
int psmtp_update_run(void *,psmtp_work *);
typedef struct psmtp_https_stream psmtp_https_stream;
/* Caller closes even a partially opened stream. Credential headers stay bound
 * to the original origin when a source redirects. */
int psmtp_https_open_link(psmtp_https_stream **, const psmtp_download_link *, psmtp_work *, uint64_t *);
int psmtp_https_http_status(const psmtp_https_stream *);
int psmtp_https_read(psmtp_https_stream *, void *, size_t, size_t *);
int psmtp_https_close(psmtp_https_stream *);
#endif
