/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PSMTP_MP4_AAC_H
#define PSMTP_MP4_AAC_H
#include "music_decoder.h"

typedef struct psmtp_mp4_aac psmtp_mp4_aac;
int psmtp_mp4_aac_open(psmtp_mp4_aac **out, psmtp_music_read_at read, void *user, uint64_t size);
void psmtp_mp4_aac_close(psmtp_mp4_aac *audio);
void psmtp_mp4_aac_seek(psmtp_mp4_aac *audio, uint64_t ms);
uint64_t psmtp_mp4_aac_next_ms(const psmtp_mp4_aac *audio);
/* One decoded packet, end of track, or a negative MTP error. */
int psmtp_mp4_aac_read(psmtp_mp4_aac *audio, int16_t *pcm, size_t capacity_bytes,
                       unsigned *bytes, unsigned *rate, unsigned *channels);
#endif
