/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#ifndef PSMTP_DIAG_LOG_H
#define PSMTP_DIAG_LOG_H
#include <stddef.h>
#include <stdint.h>
#define DIAG_LOG_LINES 2048u
#define DIAG_LOG_COLUMNS 96u
/* Receives each completed, wrapped ASCII screen line exactly once.
 * The callback must not re-enter this log. Scrolling never calls it. */
typedef void (*diag_log_line_sink)(void *, const char *, size_t);
typedef struct {
    char lines[DIAG_LOG_LINES][DIAG_LOG_COLUMNS+1];
    uint64_t first, current;
    size_t column;
    diag_log_line_sink sink;
    void *sink_user;
} diag_log;
void diag_log_init(diag_log *);
void diag_log_write(diag_log *, const char *, size_t);
void diag_log_set_sink(diag_log *, diag_log_line_sink, void *);
void diag_log_finish_line(diag_log *);
uint64_t diag_log_end(const diag_log *);
const char *diag_log_line(const diag_log *, uint64_t);
#endif
