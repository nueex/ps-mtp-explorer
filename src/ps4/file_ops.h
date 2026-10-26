/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#ifndef PSMTP_FILE_OPS_H
#define PSMTP_FILE_OPS_H
#include "file_store.h"
#include "psmtp/locations.h"
#define PSMTP_OP_SCAN 6
#define PSMTP_OP_DELETE 7
typedef struct {
    psmtp_location location;
    char path[PSMTP_LOCAL_PATH],name[256],directory[256];int wrapped;
    psmtp_native_stat native;
    mtp_object_info info;uint32_t handle;
    uint8_t phone_identity[32];int identity_valid,folder;
} psmtp_op_ref;
typedef struct {
    char path[256],target[256],name[256];psmtp_native_stat native,destination;
    uint32_t uploaded_handle;mtp_object_info uploaded_info;
    mtp_copy_source phone;size_t parent;int folder,created,verified,removed;uint8_t digest[32];
} psmtp_op_node;
typedef struct {
    psmtp_op_node nodes[MTP_PLAN_ITEMS];size_t count,files,folders,copied,removed;uint64_t bytes;
    psmtp_op_ref source;psmtp_location destination_location;char destination[256];psmtp_native_stat destination_stat;
    uint32_t phone_storage,phone_parent;mtp_object_info phone_parent_info;
    uint8_t destination_identity[32];int uncertain_upload;
    int cleanup_failed,delete_supported,uncertain_delete;FILE *log;
    const char *error_stage;int error_number;
} psmtp_op_plan;
int psmtp_op_capture(psmtp_op_ref *,const psmtp_location *,const psmtp_local_entry *);
int psmtp_op_plan_build(psmtp_op_plan *,const psmtp_op_ref *,mtp_client *,mtp_copy_progress,void *,FILE *);
int psmtp_op_target(psmtp_op_plan *,const psmtp_location *,const char *);
int psmtp_op_target_phone(psmtp_op_plan *,mtp_client *,const psmtp_location *,uint32_t,uint32_t);
int psmtp_op_space(psmtp_op_plan *,mtp_client *);
int psmtp_op_execute(psmtp_op_plan *,mtp_client *,int move,mtp_copy_progress,void *);
int psmtp_op_delete(psmtp_op_plan *,mtp_client *,int verified_move,mtp_copy_progress,void *);
int psmtp_op_mkdir(const psmtp_location *,const char *,mtp_client *,uint32_t,uint32_t,const char *,FILE *);
int psmtp_op_rename(const psmtp_op_ref *,mtp_client *,const char *,char [256],FILE *);
#endif
