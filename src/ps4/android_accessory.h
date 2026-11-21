/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PSMTP_ANDROID_ACCESSORY_H
#define PSMTP_ANDROID_ACCESSORY_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    void *handle, *devices;
    uint8_t interface_number, bulk_in, bulk_out;
    uint8_t bus, address;
    const char *stage;
    int32_t native_status;
} psmtp_android_accessory;

int psmtp_accessory_open(psmtp_android_accessory *, int (*cancelled)(void *), void *, FILE *);
int psmtp_accessory_transfer(psmtp_android_accessory *, int sending, void *, size_t, size_t *, uint32_t);
int psmtp_accessory_present(const psmtp_android_accessory *);
int psmtp_accessory_close(psmtp_android_accessory *);
#endif
