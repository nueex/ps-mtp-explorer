/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PSMTP_CLEANUP_LOG_H
#define PSMTP_CLEANUP_LOG_H
#include <stdio.h>
#include "shutdown.h"
/* Avoid stdio/file locks and formatting in the final stages. The coordinator
 * keeps a fixed memory ledger through lifecycle_mark instead. */
#ifndef PSMTP_PRODUCTION
#define fprintf(out,...) (psmtp_shutdown_logging_quiet()?0:fprintf((out),__VA_ARGS__))
#define fputs(s,out) (psmtp_shutdown_logging_quiet()?0:fputs((s),(out)))
#define fputc(c,out) (psmtp_shutdown_logging_quiet()?(c):fputc((c),(out)))
#define fflush(out) (psmtp_shutdown_logging_quiet()?0:fflush(out))
#endif
#endif
