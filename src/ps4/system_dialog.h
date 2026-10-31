/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PSMTP_SYSTEM_DIALOG_H
#define PSMTP_SYSTEM_DIALOG_H
#include <stdio.h>
/* Native PS4 message dialog; never emulated by the app renderer.
 * Returns 1 for Update/OK, 0 for Cancel, or a negative MTP status. */
int psmtp_system_dialog(const char *,int,int (*)(void *),void *,FILE *);
int psmtp_system_dialog_shutdown(FILE *);
int psmtp_system_dialog_busy(void);
#endif
