/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#ifndef PSMTP_USB_LABEL_H
#define PSMTP_USB_LABEL_H
#include <stdint.h>
#include <stddef.h>

int psmtp_usb_label(const uint8_t *,size_t,char *,size_t);
#endif
