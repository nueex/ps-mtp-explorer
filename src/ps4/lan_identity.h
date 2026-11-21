/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PSMTP_LAN_IDENTITY_H
#define PSMTP_LAN_IDENTITY_H
#include "bearssl.h"

typedef struct {
    br_ec_private_key key;
    br_x509_certificate chain;
    unsigned char scalar[32], certificate[512], token[16], pin[16];
} psmtp_lan_identity;

int psmtp_lan_identity_make(psmtp_lan_identity *, const unsigned char seed[64]);
#endif
