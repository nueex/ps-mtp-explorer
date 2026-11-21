/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#ifndef PSMTP_METADATA_H
#define PSMTP_METADATA_H
#include <stddef.h>
#include <stdint.h>

#ifndef PREVIEW_DECODE_BUDGET_BYTES
#define PREVIEW_DECODE_BUDGET_BYTES (32u * 1024u * 1024u)
#endif
#if defined(PSMTP_PROFILE_PREVIEW) && defined(PSMTP_EXPERIMENTAL_PS4_USB)
#error Host-only preview profiling must not be compiled into a PS4 build
#endif
#if !defined(PSMTP_PROFILE_PREVIEW) && (PREVIEW_DECODE_BUDGET_BYTES < 16u*1024u*1024u || PREVIEW_DECODE_BUDGET_BYTES > 32u*1024u*1024u)
#error Preview decode budget must be between 16 and 32 MiB
#endif
#define PREVIEW_ENCODED_MAX_BYTES (12u * 1024u * 1024u)
#define PREVIEW_THUMBNAIL_MAX_BYTES (256u * 256u * 4u)

typedef enum {
    PSMTP_PREVIEW_NONE=0, PSMTP_PREVIEW_OK, PSMTP_PREVIEW_ENCODED_LIMIT,
    PSMTP_PREVIEW_DIMENSIONS, PSMTP_PREVIEW_ESTIMATE_LIMIT,
    PSMTP_PREVIEW_ALLOCATION_LIMIT, PSMTP_PREVIEW_INVALID_IMAGE,
    PSMTP_PREVIEW_SYSTEM_MEMORY, PSMTP_PREVIEW_SOURCE_READ
} psmtp_preview_reason;
typedef struct {void *user;uint64_t size;int (*read)(void *,uint64_t,uint8_t *,size_t);size_t consumed;} psmtp_meta_reader;
typedef struct {
    unsigned width,height;uint64_t duration_ms,items;int items_known,loaded,error;
    char title[128],content_id[64],title_id[32],version[32],codec[24];
    uint32_t *image;unsigned image_width,image_height;
    uint64_t revision;
    /* A failed image has image==NULL; UI uses its existing compiled artwork.
     * fallback_image is a shared immutable 2x2 placeholder for other consumers. */
    const uint32_t *fallback_image;
    psmtp_preview_reason preview_reason;
    size_t decode_peak_bytes,decode_live_bytes,decode_estimate_bytes,decode_encoded_bytes;
    unsigned decode_allocations,decode_started;
} psmtp_metadata;
int psmtp_metadata_load(psmtp_meta_reader *,const char *,psmtp_metadata *);
int psmtp_metadata_mp4_audio_codec(psmtp_meta_reader *,uint32_t *);
int psmtp_metadata_image(const uint8_t *,size_t,psmtp_metadata *);
int psmtp_metadata_image_reader(psmtp_meta_reader *,uint64_t,uint64_t,psmtp_metadata *);
void psmtp_metadata_free(psmtp_metadata *);
int psmtp_decode_still(const uint8_t *,size_t,uint32_t **,unsigned *,unsigned *);
const char *psmtp_metadata_preview_reason(psmtp_preview_reason);
/* Installs the shared fallback and a reason without allocating memory. */
void psmtp_metadata_image_failed(psmtp_metadata *,psmtp_preview_reason,int);
#endif
