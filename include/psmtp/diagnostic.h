/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#ifndef PSMTP_DIAGNOSTIC_H
#define PSMTP_DIAGNOSTIC_H
#include "mtp.h"
#include <stdio.h>
#define MTP_DIAG_STORAGES 4u
#define MTP_DIAG_OBJECTS 32u

int mtp_diagnose_roots(mtp_client *, FILE *, int names);

typedef struct {
    uint16_t vendor_id,product_id;
    uint8_t mtp;
    uint64_t connection;
    char name[96];
} psmtp_ps4_candidate;

typedef struct {
    int communicate, names;
    void *user;
    void (*devices)(void *,const psmtp_ps4_candidate *,size_t);

    int (*choose_device)(void *, const psmtp_ps4_candidate *, size_t);

    int (*browse)(void *,mtp_client *,FILE *);
} psmtp_ps4_options;
int psmtp_ps4_diagnose(const psmtp_ps4_options *, FILE *);
int psmtp_ps4_poll_devices(psmtp_ps4_candidate *,size_t *,int,FILE *);
void psmtp_ps4_usb_shutdown(FILE *);
#endif
