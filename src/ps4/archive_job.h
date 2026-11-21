/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PSMTP_ARCHIVE_JOB_H
#define PSMTP_ARCHIVE_JOB_H
#include "viewer.h"

enum {PSMTP_ARCHIVE_REQUIRED=-60,PSMTP_ARCHIVE_PASSWORD=-61,PSMTP_ARCHIVE_ENCRYPTION=-62};
#define PSMTP_OP_COMPRESS 8
#define PSMTP_OP_EXTRACT 9
#define PSMTP_OP_ARCHIVE_FINALIZE 10
#define PSMTP_OP_ARCHIVE_ENCRYPT 11
enum psmtp_archive_format { PSMTP_ARCHIVE_ZIP, PSMTP_ARCHIVE_7Z, PSMTP_ARCHIVE_TAR_GZ, PSMTP_ARCHIVE_TAR,
    PSMTP_ARCHIVE_TAR_BZ2, PSMTP_ARCHIVE_TAR_XZ, PSMTP_ARCHIVE_TAR_ZST,
    PSMTP_ARCHIVE_CPIO, PSMTP_ARCHIVE_GZ, PSMTP_ARCHIVE_BZ2, PSMTP_ARCHIVE_XZ,
    PSMTP_ARCHIVE_ZST, PSMTP_ARCHIVE_FORMATS };
enum { PSMTP_COMPRESS_NAME, PSMTP_COMPRESS_FORMAT, PSMTP_COMPRESS_LEVEL,
       PSMTP_COMPRESS_PASSWORD, PSMTP_COMPRESS_DESTINATION, PSMTP_COMPRESS_START, PSMTP_COMPRESS_ROWS };
enum { PSMTP_ARCHIVE_UNPROTECTED, PSMTP_ARCHIVE_AES_ZIP, PSMTP_ARCHIVE_AES_WRAPPER };

typedef struct {
    const psmtp_op_ref *sources;
    size_t count;
    mtp_client *client;
    psmtp_location destination;
    char directory[PSMTP_LOCAL_PATH],name[256],password[256];
    char staged_directory[PSMTP_LOCAL_PATH],staged_path[PSMTP_LOCAL_PATH];
    int extracting;
    enum psmtp_archive_format format;
    unsigned level;
    uint32_t phone_storage,phone_parent;
    int uncertain_upload;
} psmtp_archive_job;

const char *psmtp_archive_suffix(const char *);
const char *psmtp_archive_format_label(enum psmtp_archive_format);
const char *psmtp_archive_extension(enum psmtp_archive_format);
int psmtp_archive_single_file(enum psmtp_archive_format);
unsigned psmtp_archive_min_level(enum psmtp_archive_format);
unsigned psmtp_archive_max_level(enum psmtp_archive_format);
int psmtp_archive_run(void *,psmtp_work *);
void psmtp_archive_forget(char [256]);
#endif
