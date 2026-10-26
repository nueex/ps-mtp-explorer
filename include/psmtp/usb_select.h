/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#ifndef PSMTP_USB_SELECT_H
#define PSMTP_USB_SELECT_H
#include <stddef.h>
#include <stdint.h>
#define USB_MAX_ALTS 32u
#define USB_MAX_ENDPOINTS 8u
#define USB_MAX_CONFIG_BYTES 4096u
typedef struct { uint8_t address, attributes; uint16_t max_packet; } usb_endpoint;
typedef struct {
    uint8_t number, alternate, cls, subclass, protocol;
    size_t endpoint_count;
    usb_endpoint endpoints[USB_MAX_ENDPOINTS];
} usb_alt;
typedef struct { uint8_t interface_number, bulk_in, bulk_out, interrupt_in; uint16_t out_max_packet,in_max_packet; } usb_mtp_interface;
/* 0=unique standard still-image candidate; -1=none; -2=ambiguous; -3=invalid. */
int usb_select_mtp(const usb_alt *, size_t, usb_mtp_interface *);

int usb_parse_config(const uint8_t *, size_t, usb_alt *, size_t *, uint8_t *);
#endif
