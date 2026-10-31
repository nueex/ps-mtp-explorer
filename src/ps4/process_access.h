/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#ifndef PSMTP_PROCESS_ACCESS_H
#define PSMTP_PROCESS_ACCESS_H
#include <stdio.h>
/* Call once before starting workers. Firmware-independent HEN capability and
 * process-layout checks; a firmware label alone does not prove compatibility.
 * 0: verified libjbc filesystem credentials;
 * 1: not available, no change attempted;
 * -1: an attempted change could not be verified; caller must stop browsing. */
int psmtp_process_access(FILE *);
/* After all filesystem users/modules close, before system LoadExec. */
int psmtp_process_access_restore(FILE *);
#endif
