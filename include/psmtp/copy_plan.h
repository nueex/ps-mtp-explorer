/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#ifndef PSMTP_COPY_PLAN_H
#define PSMTP_COPY_PLAN_H
#include "copy.h"
#define MTP_PLAN_ITEMS 512u
#define MTP_PLAN_DEPTH 10u
typedef struct { mtp_copy_source source; size_t parent,depth; int folder; } mtp_plan_item;
typedef struct {
    mtp_plan_item items[MTP_PLAN_ITEMS];
    size_t count,files,folders;
    uint64_t bytes;
} mtp_copy_plan;
typedef int (*mtp_plan_progress)(void *,size_t,size_t);
int mtp_copy_plan_build(mtp_copy_plan *,mtp_client *,uint32_t,const mtp_object_info *,mtp_plan_progress,void *,FILE *);
#endif
