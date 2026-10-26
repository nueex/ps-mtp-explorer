/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#ifndef PSMTP_PKG_H
#define PSMTP_PKG_H
#include "mtp.h"
typedef struct {void *user;uint64_t size;int (*read)(void *,uint64_t,uint8_t *,size_t);} psmtp_pkg_source;
typedef struct {uint64_t size,install_bytes;uint32_t type,flags;int patch;char content_id[37],title_id[10],package_type[6];uint8_t header_digest[32];} psmtp_pkg_info;
int psmtp_pkg_name(const char *);
int psmtp_pkg_inspect(const psmtp_pkg_source *,psmtp_pkg_info *);
typedef int (*psmtp_pkg_send)(void *,const uint8_t *,size_t);

int psmtp_pkg_http(const psmtp_pkg_source *,const char *,size_t,const char *,psmtp_pkg_send,void *);
#endif
