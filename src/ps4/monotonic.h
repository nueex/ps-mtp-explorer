/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 */
#ifndef PSMTP_ORBIS_MONOTONIC_H
#define PSMTP_ORBIS_MONOTONIC_H
#include <time.h>
/* libkernel's clock_gettime forwards the native clock ID unchanged. The SDK
 * time.h uses Linux IDs: its CLOCK_MONOTONIC=1 selects Orbis CLOCK_VIRTUAL
 * (CPU time). Orbis/FreeBSD CLOCK_MONOTONIC=4 includes blocked/sleeping time.
 * Evidence and hardware cross-check: docs/CLOCK_CORRECTION_T5_20261028.md. */
#define PSMTP_ORBIS_CLOCK_MONOTONIC 4
static inline int psmtp_monotonic_time(struct timespec *value){
    return clock_gettime(PSMTP_ORBIS_CLOCK_MONOTONIC,value);
}
#endif
