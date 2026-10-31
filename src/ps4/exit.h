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
/* Register once at startup; called on every exit route, including libc _exit. */
void psmtp_ps4_exit_set_cleanup(FILE *(*cleanup)(void));
int psmtp_ps4_exit_pending(void);
/* Never falls back to the libkernel _exit syscall that trapped on this PS4. */
__attribute__((noreturn)) void psmtp_ps4_exit(int status,FILE *log);
#endif
