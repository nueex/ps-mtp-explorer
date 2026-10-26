/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#ifndef PSMTP_DEVICE_WATCH_H
#define PSMTP_DEVICE_WATCH_H
#include "diagnostic.h"
#define PSMTP_WATCH_MAX 128u
#define PSMTP_DEVICE_POLL_MS 100u
typedef struct {uint8_t bus,address;uint16_t vendor,product;uint32_t signature;} psmtp_device_record;
typedef struct {psmtp_device_record record;uint64_t connection,retry_at,name_retry_at;int ready,mtp;unsigned name_attempts;char name[96];} psmtp_watched_device;
typedef struct {psmtp_watched_device entries[PSMTP_WATCH_MAX];size_t count;uint64_t next_connection;} psmtp_device_watch;
/* probe returns 1=MTP, 0=another device, -1=temporarily unavailable.
 * Quiet polls classify at most one new/retry device; force classifies all. */
int psmtp_device_watch_update(psmtp_device_watch *,const psmtp_device_record *,size_t,uint64_t,int,int (*)(void *,size_t),void *,psmtp_ps4_candidate *);

#define PSMTP_DEVICE_ADDED 1u
#define PSMTP_DEVICE_REMOVED 2u
unsigned psmtp_device_changes(const psmtp_ps4_candidate *,size_t,unsigned,const psmtp_ps4_candidate *,size_t,unsigned);
#endif
