/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#ifndef PSMTP_LOCATIONS_H
#define PSMTP_LOCATIONS_H
#include "diagnostic.h"
#define PSMTP_LOCATION_MAX 137u
#define PSMTP_USB_SLOTS 8u
enum { PSMTP_LOCATION_PHONE, PSMTP_LOCATION_INTERNAL, PSMTP_LOCATION_USB };
typedef struct {
    int kind; unsigned slot; uint16_t vendor_id,product_id;
    char label[96],root[256];
    uint64_t connection;
    int named;
} psmtp_location;
typedef struct {
    psmtp_location entries[PSMTP_LOCATION_MAX]; size_t count,active,selected;
} psmtp_locations;
void psmtp_locations_build(psmtp_locations *,const psmtp_ps4_candidate *,size_t,unsigned);
int psmtp_location_same(const psmtp_location *,const psmtp_location *);
int psmtp_locations_move(psmtp_locations *,int);
int psmtp_location_phone_index(const psmtp_location *,const psmtp_ps4_candidate *,size_t);
#endif
