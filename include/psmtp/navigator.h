/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#ifndef PSMTP_NAVIGATOR_H
#define PSMTP_NAVIGATOR_H
#include "mtp.h"
#include <stdio.h>
#define MTP_NAV_PAGE 16u
#define MTP_NAV_DEPTH 32u
typedef struct { uint32_t handle; mtp_object_info info; uint64_t bytes; int exact_size; } mtp_nav_entry;
typedef struct { uint32_t parent; size_t selected; char folder[MTP_STRING_BYTES]; } mtp_nav_position;
typedef struct {
    mtp_client *client;
    FILE *log;
    uint32_t storages[MTP_MAX_STORAGES],handles[MTP_MAX_ITEMS],parent;
    size_t storage_count,storage_index,count,selected,page_start,page_count,depth;
    mtp_storage_info storage;
    char folder_name[MTP_STRING_BYTES];
    mtp_nav_entry entries[MTP_NAV_PAGE];
    mtp_nav_position history[MTP_NAV_DEPTH];
    int size64_supported;
    int sort_mode;
    int rename_supported,thumbnail_supported,partial_supported,partial64_supported;
    char device_name[96];uint8_t device_identity[32];int identity_valid;
    mtp_nav_entry *catalog;
    int (*scan_progress)(void *,size_t,size_t);
    void *scan_user;
} mtp_navigator;

int mtp_is_folder(const mtp_object_info *);
int mtp_nav_open_ex(mtp_navigator *,mtp_client *,FILE *,int (*)(void *,size_t,size_t),void *);
int mtp_nav_open(mtp_navigator *,mtp_client *,FILE *);
int mtp_nav_move(mtp_navigator *,int delta);
int mtp_nav_enter(mtp_navigator *);
int mtp_nav_back(mtp_navigator *);
int mtp_nav_storage(mtp_navigator *,size_t index);
int mtp_nav_refresh(mtp_navigator *);
int mtp_nav_sort(mtp_navigator *,int);
int mtp_nav_close(mtp_navigator *);
const mtp_nav_entry *mtp_nav_selected(const mtp_navigator *);
#endif
