/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#ifndef PSMTP_PS4_EXIT_H
#define PSMTP_PS4_EXIT_H
#include <stdio.h>

__attribute__((noreturn)) void psmtp_ps4_exit(int status,FILE *log);
#endif
