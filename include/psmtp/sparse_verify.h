/* PS MTP Explorer - Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PSMTP_SPARSE_VERIFY_H
#define PSMTP_SPARSE_VERIFY_H
#include <stddef.h>
#include <stdint.h>
#define PSMTP_VERIFY_WINDOW 65536u
#define PSMTP_VERIFY_SMALL 1048576u
enum psmtp_verify_status { PSMTP_VERIFY_OK, PSMTP_VERIFY_MISMATCH, PSMTP_VERIFY_IO_ERROR, PSMTP_VERIFY_CANCELLED };
/* Callbacks return 0 or a positive errno; EINTR is retried with cancellation.
 * A successful zero-byte read before the expected end means MISMATCH. */
typedef struct {
    void *user;
    int (*size)(void *,uint64_t *);
    int (*read_at)(void *,uint64_t,uint8_t *,size_t,size_t *);
} psmtp_verify_source;
typedef struct { uint8_t *left,*right; } psmtp_verify_workspace;
typedef struct { uint64_t offset,length; } psmtp_verify_window;
typedef int (*psmtp_verify_cancel)(void *,uint64_t,uint64_t);
int psmtp_verify_workspace_init(psmtp_verify_workspace *);
void psmtp_verify_workspace_free(psmtp_verify_workspace *);
size_t psmtp_verify_plan(uint64_t,psmtp_verify_window [5]);
enum psmtp_verify_status psmtp_sparse_verify(const psmtp_verify_source *,const psmtp_verify_source *,psmtp_verify_workspace *,psmtp_verify_cancel,void *,uint64_t *);
/* Stream-only phone sources: retain just the planned bytes from the original
 * sequential pass. Maximum <1 MiB for small files, <=324 KiB for large files. */
typedef struct {
    uint8_t *bytes;psmtp_verify_window windows[5];size_t count;
    uint64_t size,consumed;
} psmtp_verify_capture;
int psmtp_verify_capture_init(psmtp_verify_capture *,uint64_t);
void psmtp_verify_capture_free(psmtp_verify_capture *);
int psmtp_verify_capture_feed(psmtp_verify_capture *,uint64_t,const uint8_t *,size_t);
psmtp_verify_source psmtp_verify_capture_source(psmtp_verify_capture *);
#endif
