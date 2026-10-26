/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#ifndef PSMTP_MTP_H
#define PSMTP_MTP_H
#include <stddef.h>
#include <stdint.h>

#define MTP_MAX_CONTAINER (1024u * 1024u)
#define MTP_MAX_ITEMS 4096u
#define MTP_MAX_STORAGES 64u
#define MTP_MAX_OPS 512u
#define MTP_STRING_BYTES 1024u
#define MTP_RX_BYTES 16384u
#define MTP_ROOT UINT32_C(0xffffffff)
#define MTP_COPY_CHUNK 65536u
#define MTP_STREAM_CHUNK (256u*1024u)



#define MTP_UPLOAD_STANDARD_MAX_SIZE UINT64_C(4294967282)
#define MTP_UPLOAD_MAX_SIZE (UINT64_MAX/8)

enum mtp_error {
    MTP_OK = 0, MTP_IO = -1, MTP_TIMEOUT = -2, MTP_DISCONNECTED = -3,
    MTP_PROTOCOL = -4, MTP_LIMIT = -5, MTP_MEMORY = -6, MTP_RESPONSE = -7,
    MTP_STATE = -8, MTP_UNSUPPORTED = -9, MTP_CANCELLED = -10, MTP_VERIFY = -11,
    MTP_FILESYSTEM = -12, MTP_NO_SPACE = -13, MTP_SPACE_UNKNOWN = -14, MTP_EXISTS = -15
};
/* Return an mtp_error; set actual even on failure. No transfer replay on error.
 * read() must accept packet-aligned MTP_RX_BYTES requests. now_ms is monotonic.
 * Adapter owns handles; client never resets/detaches/closes the USB transport. */
typedef struct {
    void *user;
    int (*read)(void *, uint8_t *, size_t, size_t *, uint32_t);
    int (*write)(void *, const uint8_t *, size_t, size_t *, uint32_t);
    uint64_t (*now_ms)(void *);
} mtp_transport;

typedef struct {
    mtp_transport io;
    uint8_t rx[MTP_RX_BYTES];
    size_t rx_pos, rx_len;
    uint32_t next_tid, last_tid, timeout_ms;
    uint16_t last_op, response;
    uint32_t response_params[5]; size_t response_param_count;
    uint16_t out_max_packet;
    int session, poisoned,content_read,session_cleanup_required,session_recovered;
    int background_io;int32_t background_native_error;
} mtp_client;

typedef struct {
    uint8_t identity[32]; int identity_valid;
    uint16_t standard_version, vendor_version, functional_mode;
    uint32_t vendor_extension;
    uint16_t operations[MTP_MAX_OPS];
    size_t operation_count;
    char vendor_description[MTP_STRING_BYTES];
    char manufacturer[MTP_STRING_BYTES], model[MTP_STRING_BYTES];
    char version[MTP_STRING_BYTES];
} mtp_device_info;

typedef struct {
    uint16_t type, filesystem, access;
    uint64_t capacity, free_bytes;
    uint32_t free_objects;
    char description[MTP_STRING_BYTES], label[MTP_STRING_BYTES];
} mtp_storage_info;

typedef struct {
    uint32_t storage, size32, parent, association_desc, sequence;
    uint16_t format, protection, association;
    uint32_t image_width,image_height;
    char name[MTP_STRING_BYTES];
    char modified[MTP_STRING_BYTES];
} mtp_object_info;

void mtp_init(mtp_client *, mtp_transport);
const char *mtp_error_string(int);
int mtp_get_device_info(mtp_client *, mtp_device_info *);
int mtp_supports(const mtp_device_info *, uint16_t);
int mtp_open_session(mtp_client *, uint32_t);
int mtp_close_session(mtp_client *);
int mtp_get_storage_ids(mtp_client *, uint32_t *, size_t, size_t *);
int mtp_get_storage_info(mtp_client *, uint32_t, mtp_storage_info *);

int mtp_get_handles(mtp_client *, uint32_t, uint32_t, uint32_t *, size_t, size_t *);
int mtp_get_object_info(mtp_client *, uint32_t, mtp_object_info *);
int mtp_delete_object(mtp_client *,uint32_t);
int mtp_get_object_size(mtp_client *,uint32_t,uint64_t *);
int mtp_get_partial(mtp_client *,uint32_t,uint64_t,size_t,int extended,uint8_t *,size_t *);
typedef int (*mtp_download_consume)(void *,const uint8_t *,size_t);
typedef int (*mtp_download_progress)(void *,uint64_t,uint64_t);
typedef struct {uint64_t read_calls,read_bytes,io_ms;} mtp_download_stats;


int mtp_get_object_stream(mtp_client *,uint32_t,uint64_t,uint8_t *,size_t,mtp_download_consume,mtp_download_progress,void *,mtp_download_stats *);
int mtp_rename_object(mtp_client *,uint32_t,uint16_t,const char *);


typedef int (*mtp_upload_read)(void *,uint8_t *,size_t);
typedef int (*mtp_upload_progress)(void *,uint64_t,uint64_t);
int mtp_send_object_info(mtp_client *,uint32_t,uint32_t,const char *,uint64_t,int,uint32_t *);
int mtp_send_object(mtp_client *,uint64_t,mtp_upload_read,mtp_upload_progress,void *);
int mtp_get_thumbnail(mtp_client *,uint32_t,uint8_t **,size_t *);


int mtp_parse_device(const uint8_t *, size_t, mtp_device_info *);
int mtp_parse_storage(const uint8_t *, size_t, mtp_storage_info *);
int mtp_parse_object(const uint8_t *, size_t, mtp_object_info *);
int mtp_parse_ids(const uint8_t *, size_t, uint32_t *, size_t, size_t *);
int mtp_decode_string(const uint8_t *, size_t, char *, size_t, size_t *);
#endif
