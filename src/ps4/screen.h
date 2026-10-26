/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#ifndef PSMTP_SCREEN_H
#define PSMTP_SCREEN_H
#include "psmtp/diag_log.h"
#include "console_render.h"
#include <stdio.h>
int psmtp_screen_open(FILE *fallback);
void psmtp_screen_draw(const diag_log *, size_t scroll, const char *status, int names, const char *save_status, const psmtp_console_selection *,const mtp_navigator *);
uint32_t psmtp_screen_buttons(void);
int psmtp_screen_pad_ready(void);
int psmtp_screen_pad_connected(void);
int32_t psmtp_screen_pad_error(void);
int32_t psmtp_screen_error(void);
void psmtp_screen_close(FILE *);
#endif
