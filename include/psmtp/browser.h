/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#ifndef PSMTP_BROWSER_H
#define PSMTP_BROWSER_H
#include "mtp.h"
#include <stdio.h>


int mtp_browser(mtp_client *, FILE *, FILE *, int);

void mtp_print_name(FILE *, const char *);
#endif
