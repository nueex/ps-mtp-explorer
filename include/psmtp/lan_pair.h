/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PSMTP_LAN_PAIR_H
#define PSMTP_LAN_PAIR_H
#include <stdint.h>

#define PSMTP_LAN_QR_VERSION 8
#define PSMTP_LAN_QR_BYTES 302

typedef struct {
    int visible, code, decision;
} psmtp_lan_pair_ui;

void psmtp_lan_commit(const uint8_t pin[16],const uint8_t nonce[16],uint8_t out[32]);
unsigned psmtp_lan_pair_code(const uint8_t pin[16],const uint8_t client[16],const uint8_t server[16]);
int psmtp_lan_pair_equal(const uint8_t *,const uint8_t *,unsigned);
#endif
