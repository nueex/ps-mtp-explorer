/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PSMTP_LAN_SPAKE_H
#define PSMTP_LAN_SPAKE_H
#include <stdint.h>
#include <stddef.h>

int psmtp_spake_scalar(const uint8_t[32]);
int psmtp_spake_share(unsigned, const uint8_t[32], int, uint8_t[65]);
int psmtp_spake_confirm(unsigned, const uint8_t[32], int, const uint8_t[65],
    const uint8_t[65], const uint8_t[16], uint8_t[64]);
void psmtp_spake_clear(void *, size_t);

#endif
