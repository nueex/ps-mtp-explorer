/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#ifndef PSMTP_LIFECYCLE_H
#define PSMTP_LIFECYCLE_H
#include <stdio.h>
#if defined(PSMTP_TEST_EXPLORER) || defined(PSMTP_TEST_PREVIEW) || defined(PSMTP_TEST_AUDIO) || defined(PSMTP_TEST_USB_RUNTIME) || defined(PSMTP_TEST_INSTALL_NATIVE) || defined(PSMTP_TEST_PROCESS_ACCESS)
static inline int psmtp_lifecycle_requested(void){
#ifdef PSMTP_TEST_CLOSE_REQUESTED
    return PSMTP_TEST_CLOSE_REQUESTED;
#else
    return 0;
#endif
}
static inline void psmtp_lifecycle_poll(void){}
static inline void psmtp_lifecycle_mark(const char *stage,int status){(void)stage;(void)status;}
static inline void psmtp_lifecycle_transfer_state(int *pause,unsigned *epoch){*pause=0;*epoch=0;}
static inline void psmtp_lifecycle_request_exit(void){}
static inline void psmtp_lifecycle_shutdown_begin(void){}
#else
int psmtp_lifecycle_open(FILE *);
/* No allocation, locks or native calls: safe to read from worker checkpoints. */
int psmtp_lifecycle_requested(void);
/* Normal worker failure intent; cleanup/exit remain on the UI owner. */
void psmtp_lifecycle_request_exit(void);
void psmtp_lifecycle_poll(void);
void psmtp_lifecycle_transfer_state(int *,unsigned *);
void psmtp_lifecycle_mark(const char *,int);
void psmtp_lifecycle_close(void);
void psmtp_lifecycle_shutdown_begin(void);
int psmtp_lifecycle_join(void *,void **,unsigned);
#endif
#endif
