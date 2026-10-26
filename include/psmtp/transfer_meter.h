/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#ifndef PSMTP_TRANSFER_METER_H
#define PSMTP_TRANSFER_METER_H
#include <stdint.h>
typedef struct {uint64_t time,bytes;} psmtp_transfer_sample;
typedef struct {
    psmtp_transfer_sample samples[24];unsigned count;
    uint64_t total,last_done,last_time,last_change,remaining_seconds;
    int active,phase,ready,eta_known;double bytes_per_second;
} psmtp_transfer_meter;
void psmtp_transfer_measure(psmtp_transfer_meter *,int,uint64_t,uint64_t,uint64_t);
void psmtp_transfer_text(const psmtp_transfer_meter *,char *,unsigned);
#endif
