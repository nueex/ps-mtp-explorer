/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#ifndef PSMTP_FILE_STORE_H
#define PSMTP_FILE_STORE_H
#include "psmtp/copy_plan.h"
#include "stat_abi.h"
#define PSMTP_LOCAL_ITEMS 4096u
#define PSMTP_LOCAL_PATH 256u
typedef struct {
    char name[MTP_STRING_BYTES],path[PSMTP_LOCAL_PATH];
    uint64_t bytes;
    int folder,verified,incomplete;
    char modified[32];
} psmtp_local_entry;
typedef struct {
    psmtp_local_entry entries[PSMTP_LOCAL_ITEMS];
    char path[PSMTP_LOCAL_PATH],root[PSMTP_LOCAL_PATH];
    size_t count,selected;
    int error,truncated,sort_mode; size_t skipped;
    const char *error_stage;
    int (*scan_progress)(void *,size_t,size_t);void *scan_user;
} psmtp_local_browser;
typedef struct { char path[PSMTP_LOCAL_PATH]; uint64_t dev,ino; int created; } psmtp_folder_node;
typedef struct {
    psmtp_folder_node nodes[MTP_PLAN_ITEMS];
    size_t count;
    FILE *log;
} psmtp_folder_store;
int psmtp_native_directory(const char *,const char *);
int psmtp_native_status(int,psmtp_native_stat *);
int psmtp_local_prepare_copies(FILE *);
unsigned psmtp_local_usb_mask(void);
unsigned psmtp_local_usb_scan(FILE *);
int psmtp_local_browse(psmtp_local_browser *,const char *,const char *);
int psmtp_local_load(psmtp_local_browser *,const char *);
int psmtp_local_enter(psmtp_local_browser *);
int psmtp_local_back(psmtp_local_browser *);
void psmtp_local_sort(psmtp_local_browser *,int);
int psmtp_store_rename_label(const char *,const char *);
int psmtp_store_remove_file_wrapper(const char *,FILE *);
int psmtp_store_receipt(const char *,const char *,uint64_t,int,const uint8_t *,const char *,FILE *);
int psmtp_folder_begin(psmtp_folder_store *,const mtp_copy_plan *,FILE *);
const char *psmtp_folder_parent(const psmtp_folder_store *,const mtp_copy_plan *,size_t);
int psmtp_folder_finish(psmtp_folder_store *,int);
#endif
