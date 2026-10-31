/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PSMTP_SHUTDOWN_H
#define PSMTP_SHUTDOWN_H
enum psmtp_shutdown_step {PSMTP_SHUTDOWN_STOP,PSMTP_SHUTDOWN_DRAIN,PSMTP_SHUTDOWN_FILES,
    PSMTP_SHUTDOWN_USB,PSMTP_SHUTDOWN_ACCESS,PSMTP_SHUTDOWN_PRESENTATION,PSMTP_SHUTDOWN_STEPS};
enum psmtp_shutdown_retained {PSMTP_RETAIN_WORK=1,PSMTP_RETAIN_PREVIEW=2,PSMTP_RETAIN_AUDIO=4,
    PSMTP_RETAIN_IME=8,PSMTP_RETAIN_INSTALL=16,PSMTP_RETAIN_USB=32,PSMTP_RETAIN_ACCESS=64,PSMTP_RETAIN_DISPLAY=128};
typedef struct {int state,first_error;unsigned attempted,finished,retained;int result[PSMTP_SHUTDOWN_STEPS];} psmtp_shutdown;
typedef int (*psmtp_shutdown_action)(void *,enum psmtp_shutdown_step,psmtp_shutdown *);
/* Once only, no allocation or I/O. Recursive/concurrent requests publish intent
 * through the caller; they never run a second cleanup or wait for the owner. */
int psmtp_shutdown_run(psmtp_shutdown *,psmtp_shutdown_action,void *);
int psmtp_shutdown_logging_quiet(void);
void psmtp_shutdown_quiet_begin(void);
#endif
