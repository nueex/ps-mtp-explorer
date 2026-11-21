/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#include "lan_identity.h"
#include <string.h>

/* Small DER encoder for the fixed-profile, self-signed P-256 leaf. Android
 * trusts this leaf only when its fingerprint matches the console display. */
static unsigned char *element(unsigned char *out, unsigned tag, const void *value, size_t n){
    *out++=(unsigned char)tag;
    if(n<128)*out++=(unsigned char)n;
    else if(n<256){*out++=0x81;*out++=(unsigned char)n;}
    else {*out++=0x82;*out++=(unsigned char)(n>>8);*out++=(unsigned char)n;}
    memcpy(out,value,n);
    return out+n;
}

int psmtp_lan_identity_make(psmtp_lan_identity *identity,const unsigned char seed[64]){
    const br_ec_impl *ec=br_ec_get_default();
    static const unsigned char signature_algorithm[]={0x30,10,6,8,0x2a,0x86,0x48,0xce,0x3d,4,3,2};
    static const unsigned char public_algorithm[]={0x30,19,6,7,0x2a,0x86,0x48,0xce,0x3d,2,1,6,8,0x2a,0x86,0x48,0xce,0x3d,3,1,7};
    static const unsigned char issuer[]={0x30,0x0e,0x31,0x0c,0x30,0x0a,6,3,0x55,4,3,0x0c,3,'P','S','4'};
    static const unsigned char validity[]={0x30,30,0x17,13,'2','0','0','1','0','1','0','0','0','0','0','0','Z',
        0x17,13,'4','9','1','2','3','1','2','3','5','9','5','9','Z'};
    br_hmac_drbg_context rng;
    br_ec_public_key public_key;
    br_sha256_context hash;
    unsigned char point[66]={0}, serial[16], subject[96], body[256], tbs[260], tail[384], signature[80]={0}, digest[32];
    unsigned char *at=body;
    size_t size, sig_size;
    memset(identity,0,sizeof(*identity));
    br_hmac_drbg_init(&rng,&br_sha256_vtable,seed,48);
    rng.vtable->generate(&rng.vtable,serial,sizeof(serial));
    serial[0]=(serial[0]&0x7f)|1;
    if(br_ec_keygen(&rng.vtable,ec,&identity->key,identity->scalar,BR_EC_secp256r1)!=32 ||
        br_ec_compute_pub(ec,&public_key,point+1,&identity->key)!=65)return -1;
    memcpy(identity->token,seed+48,sizeof(identity->token));
    at=element(at,2,serial,sizeof(serial));
    memcpy(at,signature_algorithm,sizeof(signature_algorithm));at+=sizeof(signature_algorithm);
    memcpy(at,issuer,sizeof(issuer));at+=sizeof(issuer);
    memcpy(at,validity,sizeof(validity));at+=sizeof(validity);
    memcpy(at,issuer,sizeof(issuer));at+=sizeof(issuer);
    memcpy(subject,public_algorithm,sizeof(public_algorithm));
    size=(size_t)(element(subject+sizeof(public_algorithm),3,point,sizeof(point))-subject);
    at=element(at,0x30,subject,size);
    size=(size_t)(element(tbs,0x30,body,(size_t)(at-body))-tbs);
    br_sha256_init(&hash);br_sha256_update(&hash,tbs,size);br_sha256_out(&hash,digest);
    sig_size=br_ecdsa_sign_asn1_get_default()(ec,&br_sha256_vtable,digest,&identity->key,signature+1);
    if(!sig_size || sig_size>=sizeof(signature))return -1;
    memcpy(tail,tbs,size);memcpy(tail+size,signature_algorithm,sizeof(signature_algorithm));
    at=element(tail+size+sizeof(signature_algorithm),3,signature,sig_size+1);
    identity->chain.data=identity->certificate;
    identity->chain.data_len=(size_t)(element(identity->certificate,0x30,tail,(size_t)(at-tail))-identity->certificate);
    br_sha256_init(&hash);br_sha256_update(&hash,identity->certificate,identity->chain.data_len);br_sha256_out(&hash,digest);
    memcpy(identity->pin,digest,sizeof(identity->pin));
    return 0;
}
