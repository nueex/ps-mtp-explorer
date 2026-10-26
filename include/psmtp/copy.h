/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#ifndef PSMTP_COPY_H
#define PSMTP_COPY_H
#include "psmtp/mtp.h"
#include <stdio.h>
typedef struct {
    mtp_object_info info;
    uint32_t handle;
    uint64_t size;
    int extended,size_property,is_pkg,streamed,partial_supported;
} mtp_copy_source;
enum mtp_copy_phase { MTP_COPY_CONFIRM, MTP_COPY_RECEIVE, MTP_COPY_LOCAL, MTP_COPY_SOURCE, MTP_COPY_PUBLISH, MTP_COPY_PREPARE };
typedef int (*mtp_copy_progress)(void *,enum mtp_copy_phase,uint64_t,uint64_t);


#define MTP_COPY_SPACE_STREAMING 1
/* Backend owns its temporary file. Callbacks return mtp_error except the documented space reservation result.
 * publish must NEVER replace a pre-existing target. A nonzero return may
 * still have published: set *published=1 immediately after atomic publication.
 * dispose removes ONLY the owned temporary entry, never the published file. */
typedef struct {
    void *user;
    int (*begin)(void *,const mtp_copy_source *);
    int (*space)(void *,uint64_t *);
    int (*write)(void *,const uint8_t *,size_t,size_t *);
    int (*sync)(void *);
    int (*rewind)(void *);
    int (*read)(void *,uint8_t *,size_t,size_t *);
    int (*publish)(void *,int *published);
    int (*dispose)(void *);
    int (*prepare)(void *,mtp_copy_progress,void *);
} mtp_copy_sink;
typedef struct { uint8_t digest[32]; int published,cleanup_failed; } mtp_copy_result;
int mtp_copy_probe(mtp_client *,uint32_t,const mtp_object_info *,mtp_copy_source *,FILE *);

int mtp_copy_run_quick(mtp_client *,const mtp_copy_source *,const mtp_copy_sink *,mtp_copy_progress,void *,mtp_copy_result *,FILE *);
int mtp_copy_run(mtp_client *,const mtp_copy_source *,const mtp_copy_sink *,mtp_copy_progress,void *,mtp_copy_result *,FILE *);
#endif
