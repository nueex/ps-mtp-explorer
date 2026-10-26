/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#ifndef PSMTP_USB_MODULE_H
#define PSMTP_USB_MODULE_H
#include <stdint.h>
#include <stdio.h>



typedef struct {
    void *user;
    int32_t (*is_loaded)(void *);
    int32_t (*load)(void *);
    int32_t (*find_handle)(void *, int32_t *);
    int32_t (*unload)(void *);
} psmtp_usb_module_ops;
typedef struct { int32_t handle; int owns_reference; } psmtp_usb_module;

int psmtp_usb_module_acquire(psmtp_usb_module *, const psmtp_usb_module_ops *, FILE *);
int psmtp_usb_module_release(psmtp_usb_module *, const psmtp_usb_module_ops *, FILE *);
#endif
