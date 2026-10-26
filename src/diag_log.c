/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#include "psmtp/diag_log.h"
#include <string.h>
void diag_log_init(diag_log *log) { memset(log,0,sizeof(*log)); }
static void next_line(diag_log *log) {
    if(log->sink) log->sink(log->sink_user,log->lines[log->current%DIAG_LOG_LINES],log->column);
    ++log->current; log->column=0;
    if(log->current-log->first>=DIAG_LOG_LINES) log->first=log->current-DIAG_LOG_LINES+1;
    log->lines[log->current%DIAG_LOG_LINES][0]=0;
}
void diag_log_write(diag_log *log,const char *text,size_t n) {
    size_t i;
    for(i=0;i<n;++i) {
        unsigned char c=(unsigned char)text[i]; char *line;
        if(c=='\r') continue;
        if(c=='\n') { next_line(log); continue; }
        if(log->column==DIAG_LOG_COLUMNS) next_line(log);
        line=log->lines[log->current%DIAG_LOG_LINES];


        line[log->column++]=(char)(c>=32 && c<127?c:'?');
        line[log->column]=0;
    }
}
void diag_log_set_sink(diag_log *log,diag_log_line_sink sink,void *user) {
    log->sink=sink; log->sink_user=user;
}
void diag_log_finish_line(diag_log *log) { if(log->column) next_line(log); }
uint64_t diag_log_end(const diag_log *log) { return log->current+(log->column?1u:0u); }
const char *diag_log_line(const diag_log *log,uint64_t index) {
    if(index<log->first || index>=diag_log_end(log)) return NULL;
    return log->lines[index%DIAG_LOG_LINES];
}
