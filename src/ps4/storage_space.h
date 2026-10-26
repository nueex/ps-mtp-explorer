/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#ifndef PSMTP_STORAGE_SPACE_H
#define PSMTP_STORAGE_SPACE_H
#include <stdint.h>
#include <stdio.h>
int psmtp_native_space(const char *,const char *,uint64_t *,uint64_t *,FILE *);


int psmtp_space_require(const char *,const char *,uint64_t,FILE *);
#endif
