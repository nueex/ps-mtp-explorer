/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PSMTP_MEDIA_PLAYER_H
#define PSMTP_MEDIA_PLAYER_H
#include "viewer.h"

typedef struct psmtp_media_player psmtp_media_player;
typedef struct {
    const uint32_t *pixels;
    unsigned width,height,volume;
    float aspect;
    uint64_t time,duration;
    int paused,ended,loading,audio_error;
    uint64_t sequence;
} psmtp_media_frame;

int psmtp_media_open(psmtp_media_player **,const psmtp_viewer *);
int psmtp_media_update(psmtp_media_player *,psmtp_media_frame *);
int psmtp_media_pause(psmtp_media_player *);
int psmtp_media_seek(psmtp_media_player *,int);
int psmtp_media_system_pause(psmtp_media_player *,int);
void psmtp_media_volume(psmtp_media_player *,int);
int psmtp_media_close(psmtp_media_player *);
int psmtp_media_close_poll(psmtp_media_player *,unsigned wait_ms);
#endif
