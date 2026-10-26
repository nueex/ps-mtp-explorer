/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#ifndef PSMTP_COPY_FILE_H
#define PSMTP_COPY_FILE_H
#include "psmtp/copy.h"
#include "psmtp/copy_paths.h"
typedef struct {
    int data,base,root,parent,directory,file,owned,owned_directory,published,directory_readable;
    uint64_t data_dev,data_ino,base_dev,base_ino,root_dev,root_ino,dir_dev,dir_ino,file_dev,file_ino;
    uint64_t expected_size;
    uint64_t parent_dev,parent_ino;
    char parent_path[256],directory_name[80],directory_path[256],temporary[256],path[256];
    char error_stage[40]; int error_number;
    FILE *log;
} psmtp_copy_file;

int psmtp_copy_file_discard(psmtp_copy_file *);
void psmtp_copy_file_init(psmtp_copy_file *,FILE *,mtp_copy_sink *);
int psmtp_copy_file_init_at(psmtp_copy_file *,FILE *,mtp_copy_sink *,const char *);
#endif
