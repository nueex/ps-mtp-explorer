/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#ifndef PSMTP_RELEASE_CONFIG_H
#define PSMTP_RELEASE_CONFIG_H
#include <stdio.h>
#ifdef PSMTP_PRODUCTION
/* Adapters require a valid FILE even when their logging calls are compiled out.
 * Reuse stdout as the contract token without creating a persistent log file. */
static inline FILE *psmtp_release_log_stream(void) { return stdout; }
static __attribute__((always_inline)) inline int psmtp_quiet_print(FILE *out,const char *format,...) { (void)out;(void)format;return 0; }
static __attribute__((always_inline)) inline int psmtp_quiet_text(const char *text,FILE *out) { (void)text;(void)out;return 0; }
static __attribute__((always_inline)) inline int psmtp_quiet_char(int c,FILE *out) { (void)c;(void)out;return c; }
static __attribute__((always_inline)) inline int psmtp_quiet_flush(FILE *out) { (void)out;return 0; }
#define fprintf psmtp_quiet_print
#define fputs psmtp_quiet_text
#define fputc psmtp_quiet_char
#define fflush psmtp_quiet_flush
#endif
#endif
