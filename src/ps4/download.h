/* PS MTP Explorer - Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * See LICENSE and LICENSING.md. WITHOUT ANY WARRANTY. */
#ifndef PSMTP_PS4_DOWNLOAD_H
#define PSMTP_PS4_DOWNLOAD_H
#include "psmtp/copy.h"
int psmtp_stream_download(mtp_client *,uint32_t,uint64_t,uint8_t *,size_t,mtp_download_consume,mtp_download_progress,void *,mtp_download_stats *,FILE *);
/* Off for new users. Startup restores the saved choice before USB work.
 * Applies to future files; never replay an active stream. */
void psmtp_stream_boost_set(int);
int psmtp_stream_boost_get(void);
/* Contract models have no native thread. The threaded executor is tested
 * separately with real host threads and ordered/failing sinks. */
#if defined(PSMTP_TEST_FILE_SYSTEM) || defined(PSMTP_TEST_INSTALL_NATIVE)
#define PSMTP_DOWNLOAD_READER NULL
#else
#define PSMTP_DOWNLOAD_READER psmtp_stream_download
#endif
#endif
