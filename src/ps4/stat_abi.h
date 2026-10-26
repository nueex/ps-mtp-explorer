/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
/* PS4 x86-64 fstat wire ABI, independent of SDK musl mode_t.
 * ABI references and the SDK mismatch are recorded in docs/COPY_0.4.5.md.
 * These fixed-width declarations describe the native data interface. */
#ifndef PSMTP_STAT_ABI_H
#define PSMTP_STAT_ABI_H
#include <stdint.h>
#include <stddef.h>
typedef struct { int64_t seconds,nanoseconds; } psmtp_stat_time;
typedef struct {
    uint32_t st_dev,st_ino;
    uint16_t st_mode,st_nlink;
    uint32_t st_uid,st_gid,st_rdev;
    psmtp_stat_time access_time,modify_time,change_time;
    int64_t st_size,st_blocks;
    uint32_t st_blksize,st_flags,st_gen;
    int32_t spare;
    psmtp_stat_time birth_time;
} psmtp_native_stat;
typedef char psmtp_stat_layout[(sizeof(psmtp_native_stat)==120 &&
    offsetof(psmtp_native_stat,st_dev)==0 && offsetof(psmtp_native_stat,st_ino)==4 &&
    offsetof(psmtp_native_stat,st_mode)==8 && offsetof(psmtp_native_stat,st_nlink)==10 &&
    offsetof(psmtp_native_stat,access_time)==24 && offsetof(psmtp_native_stat,st_size)==72 &&
    offsetof(psmtp_native_stat,st_blocks)==80 && offsetof(psmtp_native_stat,st_blksize)==88 &&
    offsetof(psmtp_native_stat,birth_time)==104)?1:-1];
#endif
