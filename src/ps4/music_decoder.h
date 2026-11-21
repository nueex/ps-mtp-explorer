/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PSMTP_MUSIC_DECODER_H
#define PSMTP_MUSIC_DECODER_H
#include <stddef.h>
#include <stdint.h>

typedef struct psmtp_music_decoder psmtp_music_decoder;
typedef int (*psmtp_music_read_at)(void *,uint8_t *,uint64_t,uint32_t);
typedef struct {unsigned channels,rate;uint64_t frames;} psmtp_music_info;

int psmtp_music_open(psmtp_music_decoder **,const char *,psmtp_music_read_at,void *,uint64_t,psmtp_music_info *);
int psmtp_music_read(psmtp_music_decoder *,int16_t *,unsigned);
int psmtp_music_seek(psmtp_music_decoder *,uint64_t);
void psmtp_music_close(psmtp_music_decoder *);
#endif
