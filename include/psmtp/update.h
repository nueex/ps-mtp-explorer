/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PSMTP_UPDATE_H
#define PSMTP_UPDATE_H
#include "mtp.h"
#include "version.h"
#define PSMTP_UPDATE_MANIFEST_LIMIT 4096u
#define PSMTP_UPDATE_MAX_BYTES (512u*1024u*1024u)
#define PSMTP_UPDATE_CONTENT_ID "IV0000-" PSMTP_TITLE_ID "_00-NUEEXMTP00000000"
typedef struct {
    int enabled;char version[6],url[2048];uint64_t size;uint8_t sha256[32];
} psmtp_update_manifest;
/* Strict bounded manifest, no partial output on error. HTTPS only. */
int psmtp_update_parse(const char *,size_t,psmtp_update_manifest *);
int psmtp_update_version(const char *);
int psmtp_update_newer(const psmtp_update_manifest *,const char *);
int psmtp_update_https(const char *);
#endif
