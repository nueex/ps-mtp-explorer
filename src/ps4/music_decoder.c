/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#include "music_decoder.h"
#include "psmtp/mtp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_SIMD
#define DR_FLAC_NO_OGG
#define DR_MP3_IMPLEMENTATION
#define DR_MP3_NO_STDIO
#define DR_MP3_NO_SIMD
#define DR_WAV_IMPLEMENTATION
#define DR_WAV_NO_STDIO

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif
#include "../../third_party/dr_libs/dr_flac.h"
#include "../../third_party/dr_libs/dr_mp3.h"
#include "../../third_party/dr_libs/dr_wav.h"
#ifdef __clang__
#pragma clang diagnostic pop
#endif

enum {MUSIC_MP3=1,MUSIC_FLAC,MUSIC_WAV};
struct psmtp_music_decoder {
    int kind,initialized,error;
    psmtp_music_read_at read;
    void *source;
    uint64_t size,offset;
    union {drmp3 mp3;drflac *flac;drwav wav;} file;
};

static size_t sound_read(void *context,void *buffer,size_t length){
    psmtp_music_decoder *decoder=context;
    if(decoder->offset>=decoder->size)return 0;
    if(length>decoder->size-decoder->offset)length=(size_t)(decoder->size-decoder->offset);
    if(length>INT32_MAX)length=INT32_MAX;
    int n=decoder->read(decoder->source,buffer,decoder->offset,(uint32_t)length);
    if(n<0 || (size_t)n>length){decoder->error=1;return 0;}
    decoder->offset+=(uint64_t)n;
    return (size_t)n;
}

static int sound_seek(void *context,int distance,int origin){
    psmtp_music_decoder *decoder=context;
    uint64_t base=origin==SEEK_CUR?decoder->offset:origin==SEEK_END?decoder->size:0;
    if(distance<0 && (uint64_t)-(int64_t)distance>base)return 0;
    uint64_t offset=base+(uint64_t)(int64_t)distance;
    if(offset>decoder->size)return 0;
    decoder->offset=offset;
    return 1;
}

static drmp3_bool32 mp3_seek(void *p,int n,drmp3_seek_origin o){return sound_seek(p,n,o==DRMP3_SEEK_CUR?SEEK_CUR:o==DRMP3_SEEK_END?SEEK_END:SEEK_SET);}
static drflac_bool32 flac_seek(void *p,int n,drflac_seek_origin o){return sound_seek(p,n,o==DRFLAC_SEEK_CUR?SEEK_CUR:o==DRFLAC_SEEK_END?SEEK_END:SEEK_SET);}
static drwav_bool32 wav_seek(void *p,int n,drwav_seek_origin o){return sound_seek(p,n,o==DRWAV_SEEK_CUR?SEEK_CUR:o==DRWAV_SEEK_END?SEEK_END:SEEK_SET);}
static drmp3_bool32 mp3_tell(void *p,drmp3_int64 *offset){*offset=(drmp3_int64)((psmtp_music_decoder *)p)->offset;return 1;}
static drflac_bool32 flac_tell(void *p,drflac_int64 *offset){*offset=(drflac_int64)((psmtp_music_decoder *)p)->offset;return 1;}
static drwav_bool32 wav_tell(void *p,drwav_int64 *offset){*offset=(drwav_int64)((psmtp_music_decoder *)p)->offset;return 1;}

int psmtp_music_open(psmtp_music_decoder **out,const char *name,psmtp_music_read_at read,void *source,uint64_t size,psmtp_music_info *info){
    const char *extension=strrchr(name,'.');
    *out=NULL;
    if(!extension)return MTP_UNSUPPORTED;
    int kind=!strcasecmp(extension,".mp3")?MUSIC_MP3:!strcasecmp(extension,".flac")?MUSIC_FLAC:!strcasecmp(extension,".wav")?MUSIC_WAV:0;
    if(!kind)return MTP_UNSUPPORTED;
    psmtp_music_decoder *d=calloc(1,sizeof(*d));
    if(!d)return MTP_MEMORY;
    d->read=read;d->source=source;d->size=size;d->kind=kind;
    if(kind==MUSIC_MP3){
        d->initialized=drmp3_init(&d->file.mp3,sound_read,mp3_seek,mp3_tell,NULL,d,NULL);
        if(d->initialized)*info=(psmtp_music_info){d->file.mp3.channels,d->file.mp3.sampleRate,
            size<=64u*1024u*1024u?drmp3_get_pcm_frame_count(&d->file.mp3):0};
    }else if(kind==MUSIC_FLAC){
        d->file.flac=drflac_open(sound_read,flac_seek,flac_tell,d,NULL);d->initialized=d->file.flac!=NULL;
        if(d->initialized)*info=(psmtp_music_info){d->file.flac->channels,d->file.flac->sampleRate,d->file.flac->totalPCMFrameCount};
    }else {
        d->initialized=drwav_init(&d->file.wav,sound_read,wav_seek,wav_tell,d,NULL);
        if(d->initialized)*info=(psmtp_music_info){d->file.wav.channels,d->file.wav.sampleRate,d->file.wav.totalPCMFrameCount};
    }
    if(!d->initialized || d->error || !info->channels || info->channels>8 || info->rate<8000 || info->rate>192000){
        int rc=d->error?MTP_IO:MTP_PROTOCOL;psmtp_music_close(d);return rc;
    }
    *out=d;
    return 0;
}

int psmtp_music_read(psmtp_music_decoder *d,int16_t *samples,unsigned frames){
    uint64_t count=d->kind==MUSIC_MP3?drmp3_read_pcm_frames_s16(&d->file.mp3,frames,samples):
        d->kind==MUSIC_WAV?drwav_read_pcm_frames_s16(&d->file.wav,frames,samples):drflac_read_pcm_frames_s16(d->file.flac,frames,samples);
    return d->error?-1:(int)count;
}

int psmtp_music_seek(psmtp_music_decoder *d,uint64_t frame){
    int ok=d->kind==MUSIC_MP3?drmp3_seek_to_pcm_frame(&d->file.mp3,frame):
        d->kind==MUSIC_WAV?drwav_seek_to_pcm_frame(&d->file.wav,frame):drflac_seek_to_pcm_frame(d->file.flac,frame);
    return ok && !d->error?0:MTP_IO;
}

void psmtp_music_close(psmtp_music_decoder *d){
    if(!d)return;
    if(d->initialized){
        if(d->kind==MUSIC_MP3)drmp3_uninit(&d->file.mp3);
        else if(d->kind==MUSIC_WAV)drwav_uninit(&d->file.wav);
        else drflac_close(d->file.flac);
    }
    free(d);
}
