/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#include "psmtp/lan_spake.h"
#include "psmtp/sha256.h"
#include "bearssl_ec.h"
#include <string.h>

/* RFC 9382, section 6. SEC1 encodings of the P-256 M/N points and inverses. */
static const uint8_t M[65] = {
    0x04, 0x88, 0x6e, 0x2f, 0x97, 0xac, 0xe4, 0x6e, 0x55, 0xba, 0x9d, 0xd7, 0x24,
    0x25, 0x79, 0xf2, 0x99, 0x3b, 0x64, 0xe1, 0x6e, 0xf3, 0xdc, 0xab, 0x95, 0xaf,
    0xd4, 0x97, 0x33, 0x3d, 0x8f, 0xa1, 0x2f, 0x5f, 0xf3, 0x55, 0x16, 0x3e, 0x43,
    0xce, 0x22, 0x4e, 0x0b, 0x0e, 0x65, 0xff, 0x02, 0xac, 0x8e, 0x5c, 0x7b, 0xe0,
    0x94, 0x19, 0xc7, 0x85, 0xe0, 0xca, 0x54, 0x7d, 0x55, 0xa1, 0x2e, 0x2d, 0x20,
};

static const uint8_t M_neg[65] = {
    0x04, 0x88, 0x6e, 0x2f, 0x97, 0xac, 0xe4, 0x6e, 0x55, 0xba, 0x9d, 0xd7, 0x24,
    0x25, 0x79, 0xf2, 0x99, 0x3b, 0x64, 0xe1, 0x6e, 0xf3, 0xdc, 0xab, 0x95, 0xaf,
    0xd4, 0x97, 0x33, 0x3d, 0x8f, 0xa1, 0x2f, 0xa0, 0x0c, 0xaa, 0xe8, 0xc1, 0xbc,
    0x31, 0xde, 0xb1, 0xf4, 0xf1, 0x9a, 0x00, 0xfd, 0x53, 0x71, 0xa3, 0x84, 0x1f,
    0x6c, 0xe6, 0x38, 0x7a, 0x1f, 0x35, 0xab, 0x82, 0xaa, 0x5e, 0xd1, 0xd2, 0xdf,
};

static const uint8_t N[65] = {
    0x04, 0xd8, 0xbb, 0xd6, 0xc6, 0x39, 0xc6, 0x29, 0x37, 0xb0, 0x4d, 0x99, 0x7f,
    0x38, 0xc3, 0x77, 0x07, 0x19, 0xc6, 0x29, 0xd7, 0x01, 0x4d, 0x49, 0xa2, 0x4b,
    0x4f, 0x98, 0xba, 0xa1, 0x29, 0x2b, 0x49, 0x07, 0xd6, 0x0a, 0xa6, 0xbf, 0xad,
    0xe4, 0x50, 0x08, 0xa6, 0x36, 0x33, 0x7f, 0x51, 0x68, 0xc6, 0x4d, 0x9b, 0xd3,
    0x60, 0x34, 0x80, 0x8c, 0xd5, 0x64, 0x49, 0x0b, 0x1e, 0x65, 0x6e, 0xdb, 0xe7,
};

static const uint8_t N_neg[65] = {
    0x04, 0xd8, 0xbb, 0xd6, 0xc6, 0x39, 0xc6, 0x29, 0x37, 0xb0, 0x4d, 0x99, 0x7f,
    0x38, 0xc3, 0x77, 0x07, 0x19, 0xc6, 0x29, 0xd7, 0x01, 0x4d, 0x49, 0xa2, 0x4b,
    0x4f, 0x98, 0xba, 0xa1, 0x29, 0x2b, 0x49, 0xf8, 0x29, 0xf5, 0x58, 0x40, 0x52,
    0x1b, 0xb0, 0xf7, 0x59, 0xc9, 0xcc, 0x80, 0xae, 0x97, 0x39, 0xb2, 0x64, 0x2c,
    0xa0, 0xcb, 0x7f, 0x73, 0x2a, 0x9b, 0xb6, 0xf4, 0xe1, 0x9a, 0x91, 0x24, 0x18,
};

static const uint8_t one[32] = {[31] = 1};
static const char phone_id[] = "PSMTP phone";
static const char console_id[] = "PSMTP PS4";

void psmtp_spake_clear(void *data, size_t size) {
    volatile uint8_t *bytes = data;
    while (size--) *bytes++ = 0;
}

int psmtp_spake_scalar(const uint8_t scalar[32]) {
    size_t length;
    const uint8_t *order = br_ec_p256_m31.order(BR_EC_secp256r1, &length);
    uint8_t nonzero = 0;
    for (size_t i = 0; i < 32; ++i) nonzero |= scalar[i];
    return nonzero && length == 32 && memcmp(scalar, order, 32) < 0;
}

static void password(uint8_t scalar[32], unsigned code) {
    unsigned value = code + 1;
    memset(scalar, 0, 32);
    scalar[29] = (uint8_t)(value >> 16);
    scalar[30] = (uint8_t)(value >> 8);
    scalar[31] = (uint8_t)value;
}

int psmtp_spake_share(unsigned code, const uint8_t scalar[32], int phone, uint8_t point[65]) {
    uint8_t w[32];
    if (code >= 1000000 || !psmtp_spake_scalar(scalar)) return 0;
    password(w, code);
    memcpy(point, phone ? M : N, 65);
    int ok = br_ec_p256_m31.muladd(point, NULL, 65, w, sizeof(w), scalar, 32, BR_EC_secp256r1);
    psmtp_spake_clear(w, sizeof(w));
    return ok;
}

static void hmac(const uint8_t *key, size_t size, const uint8_t *data, size_t length, uint8_t output[32]) {
    psmtp_sha256 hash;
    uint8_t pad[64], inner[32];
    memset(pad, 0x36, sizeof(pad));
    for (size_t i = 0; i < size; ++i) pad[i] ^= key[i];
    psmtp_sha256_init(&hash);
    psmtp_sha256_update(&hash, pad, sizeof(pad));
    psmtp_sha256_update(&hash, data, length);
    psmtp_sha256_final(&hash, inner);
    for (size_t i = 0; i < sizeof(pad); ++i) pad[i] ^= 0x36 ^ 0x5c;
    psmtp_sha256_init(&hash);
    psmtp_sha256_update(&hash, pad, sizeof(pad));
    psmtp_sha256_update(&hash, inner, sizeof(inner));
    psmtp_sha256_final(&hash, output);
    psmtp_spake_clear(&hash, sizeof(hash));
    psmtp_spake_clear(pad, sizeof(pad));
    psmtp_spake_clear(inner, sizeof(inner));
}

int psmtp_spake_confirm(unsigned code, const uint8_t scalar[32], int phone, const uint8_t a[65],
    const uint8_t b[65], const uint8_t pin[16], uint8_t proofs[64]) {
    uint8_t w[32], shared[65], digest[32], extract[32], keys[32];
    uint8_t info[sizeof("ConfirmationKeysPSMTP3") - 1 + 16 + 1] = "ConfirmationKeysPSMTP3";
    uint8_t transcript[8*6 + sizeof(phone_id)-1 + sizeof(console_id)-1 + 65*3 + 32];
    static const uint8_t salt[32] = {0};
    psmtp_sha256 hash;
    if (code >= 1000000 || !psmtp_spake_scalar(scalar)) return 0;
    password(w, code);
    memcpy(shared, phone ? b : a, sizeof(shared));
    int ok = br_ec_p256_m31.muladd(shared, phone ? N_neg : M_neg, sizeof(shared), one, sizeof(one),
        w, sizeof(w), BR_EC_secp256r1);
    if (ok) ok = br_ec_p256_m31.mul(shared, sizeof(shared), scalar, 32, BR_EC_secp256r1);
    if (ok) {
        const struct {const uint8_t *data; uint8_t size;} items[] = {
            {(const uint8_t *)phone_id, sizeof(phone_id)-1}, {(const uint8_t *)console_id, sizeof(console_id)-1},
            {a, 65}, {b, 65}, {shared, 65}, {w, 32}
        };
        size_t cursor = 0;
        memset(transcript, 0, sizeof(transcript));
        for (size_t i = 0; i < sizeof(items)/sizeof(*items); ++i) {
            transcript[cursor] = items[i].size;
            memcpy(transcript + cursor + 8, items[i].data, items[i].size);
            cursor += 8 + items[i].size;
        }
        psmtp_sha256_init(&hash);
        psmtp_sha256_update(&hash, transcript, cursor);
        psmtp_sha256_final(&hash, digest);
        memcpy(info + sizeof(info) - 17, pin, 16);
        info[sizeof(info)-1] = 1;
        hmac(salt, sizeof(salt), digest + 16, 16, extract);
        hmac(extract, sizeof(extract), info, sizeof(info), keys);
        hmac(keys, 16, transcript, cursor, proofs);
        hmac(keys + 16, 16, transcript, cursor, proofs + 32);
    }
    psmtp_spake_clear(w, sizeof(w));
    psmtp_spake_clear(shared, sizeof(shared));
    psmtp_spake_clear(&hash, sizeof(hash));
    psmtp_spake_clear(transcript, sizeof(transcript));
    psmtp_spake_clear(digest, sizeof(digest));
    psmtp_spake_clear(extract, sizeof(extract));
    psmtp_spake_clear(keys, sizeof(keys));
    return ok;
}
