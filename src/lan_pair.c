/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#include "psmtp/lan_pair.h"
#include "psmtp/sha256.h"

void psmtp_lan_commit(const uint8_t pin[16],const uint8_t nonce[16],uint8_t out[32]){
    psmtp_sha256 hash;
    psmtp_sha256_init(&hash);
    psmtp_sha256_update(&hash,"C",1);
    psmtp_sha256_update(&hash,pin,16);
    psmtp_sha256_update(&hash,nonce,16);
    psmtp_sha256_final(&hash,out);
}

unsigned psmtp_lan_pair_code(const uint8_t pin[16],const uint8_t client[16],const uint8_t server[16]){
    psmtp_sha256 hash;
    uint8_t out[32];
    psmtp_sha256_init(&hash);
    psmtp_sha256_update(&hash,"V",1);
    psmtp_sha256_update(&hash,pin,16);
    psmtp_sha256_update(&hash,client,16);
    psmtp_sha256_update(&hash,server,16);
    psmtp_sha256_final(&hash,out);
    uint32_t number=(uint32_t)out[0]<<24 | (uint32_t)out[1]<<16 | (uint32_t)out[2]<<8 | out[3];
    return number%1000000u;
}

int psmtp_lan_pair_equal(const uint8_t *left,const uint8_t *right,unsigned bytes){
    uint8_t different=0;
    for(unsigned i=0;i<bytes;++i)different|=left[i]^right[i];
    return different==0;
}
