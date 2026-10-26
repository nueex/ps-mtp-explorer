/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#ifndef PSMTP_NAME_H
#define PSMTP_NAME_H
#include <stddef.h>
#include <stdint.h>
int psmtp_name_encode(const char *,uint8_t *,size_t,size_t *);
int psmtp_name_valid(const char *);
#endif
