/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#include "media_player.h"
#include "optional_module.h"
#include "lifecycle.h"
#include "monotonic.h"
#include "native_sync.h"
#include "music_decoder.h"
#include "mp4_aac.h"
#include "psmtp/metadata.h"
#include <orbis/AudioOut.h>
#include <orbis/UserService.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <strings.h>

/* The OpenOrbis AvPlayer header declares untyped exports. These are the
 * 64-bit native layouts used by the AvPlayer file, frame and stream APIs. */
typedef struct {
    void *memory_context;
    void *(*allocate)(void *,uint32_t,uint32_t);
    void (*release)(void *,void *);
    void *(*allocate_texture)(void *,uint32_t,uint32_t);
    void (*release_texture)(void *,void *);
    void *file_context;
    int (*open_file)(void *,const char *);
    int (*close_file)(void *);
    int (*read_file)(void *,uint8_t *,uint64_t,uint32_t);
    uint64_t (*size_file)(void *);
    void *event_context;
    void (*event)(void *,int,int,void *);
    uint32_t debug,priority;
    int32_t buffers;
    bool auto_start;
    uint8_t reserved[3];
    const char *language;
} av_init;

typedef struct {uint16_t channels,reserved;uint32_t rate,bytes;char language[4];} av_audio;
typedef struct {
    uint32_t width,height;
    float aspect;
    char language[4];
    uint32_t frame_rate,left,right,top,bottom,pitch;
    uint8_t luma_depth,chroma_depth,full_range,reserved[37];
} av_video;
typedef struct {const uint8_t *data;uint64_t reserved,time;av_audio audio;} av_audio_frame;
typedef struct {const uint8_t *data;uint64_t reserved,time;av_video video;} av_video_frame;
typedef struct {uint32_t type,reserved;av_audio audio;uint64_t duration,start;} av_stream;
typedef struct {
    uint32_t video_buffer_size,video_decoder,video_decoder_params[7];
    uint32_t audio_decoder;
    uint8_t audio_channel_order,audio_reserved[27];
    uint32_t http_context,ssl_context;
    uint8_t reserved[56];
} av_post_init;
typedef char av_abi[(sizeof(av_init)==120 && sizeof(av_audio_frame)==40 && sizeof(av_video_frame)==104 && sizeof(av_stream)==40)?1:-1];
typedef char av_post_abi[sizeof(av_post_init)==132?1:-1];

static const enum OrbisSysModule decoder_modules[]={
    ORBIS_SYSMODULE_AUDIODEC,ORBIS_SYSMODULE_AUDIODEC_CPU,ORBIS_SYSMODULE_AUDIODEC_CPU_M4AAC,
    ORBIS_SYSMODULE_AUDIODEC_CPU_DDP
};

typedef struct {
    uint32_t *pixels;
    unsigned width,height;
    float aspect;
} video_surface;

struct psmtp_media_player {
    void *handle;
    void *(*init)(av_init *);
    int (*add)(void *,const char *),(*close)(void *),(*pause)(void *),(*resume)(void *),(*start)(void *),(*stop_native)(void *);
    int (*jump)(void *,uint64_t),(*stream_count)(void *),(*stream_info)(void *,uint32_t,av_stream *);
    int (*enable_stream)(void *,uint32_t);
    bool (*video)(void *,av_video_frame *),(*audio)(void *,av_audio_frame *),(*active)(void *);
    uint64_t (*current_time)(void *);
    av_init config;
    int fd,owned,thread_started,stop,audio_error,paused,ended;
    unsigned owned_decoders;
    int needs_video,ready,start_sent,got_audio,audio_enabled,system_paused;
    unsigned volume,previous_volume,seek_epoch;
    uint64_t size,duration,time,opened,video_pts,sequence;
    int have_video_pts,closing,close_done,close_joined,close_error;
    video_surface surfaces[3];
    int video_started,shown,pending,video_processing,video_error;
    OrbisPthread thread,video_thread,close_thread;
    psmtp_sync gate;
    psmtp_music_decoder *music;
    psmtp_music_info music_info;
    uint64_t music_position;
    int music_eof;
    int software_audio;
};

static void *video_loop(void *);

static uint64_t media_now(void) {
    struct timespec t;
    return psmtp_monotonic_time(&t)?0:(uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000;
}

static void *av_alloc(void *unused,uint32_t align,uint32_t size) {
    void *p=NULL;(void)unused;
    if(align<sizeof(void *))align=sizeof(void *);
    if(size>128u*1024u*1024u || posix_memalign(&p,align,size))return NULL;
    return p;
}
static void av_free(void *unused,void *p){(void)unused;free(p);}

/* AvPlayer's decoder writes these surfaces through the GPU. Keep the
 * allocation record in the aligned prefix so callbacks need no shared list. */
typedef struct {void *base;size_t length;off_t offset;} texture_mapping;
static void *texture_alloc(void *unused,uint32_t align,uint32_t size){
    const size_t page=16384;
    (void)unused;
    if(!size || size>128u*1024u*1024u || align>128u*1024u*1024u)return NULL;
    if(align<page)align=(uint32_t)page;
    if(align&(align-1))return NULL;
    size_t length=align+(((size_t)size+page-1)&~(page-1));
    off_t offset;
    void *base=NULL;
    if(sceKernelAllocateDirectMemory(0,sceKernelGetDirectMemorySize(),length,align,ORBIS_KERNEL_WB_ONION,&offset))return NULL;
    if(sceKernelMapDirectMemory(&base,length,0x33,0,offset,align)){
        sceKernelReleaseDirectMemory(offset,length);
        return NULL;
    }
    uint8_t *surface=(uint8_t *)base+align;
    texture_mapping *record=(texture_mapping *)surface-1;
    *record=(texture_mapping){base,length,offset};
    return surface;
}
static void texture_free(void *unused,void *surface){
    (void)unused;
    if(!surface)return;
    texture_mapping mapping=*((texture_mapping *)surface-1);
    if(!sceKernelMunmap(mapping.base,mapping.length))sceKernelReleaseDirectMemory(mapping.offset,mapping.length);
}
static int file_open(void *user,const char *name){(void)name;return ((psmtp_media_player *)user)->fd<0?-1:0;}
static int file_close(void *user){(void)user;return 0;}
static uint64_t file_size(void *user){return ((psmtp_media_player *)user)->size;}
static int file_read(void *user,uint8_t *buffer,uint64_t position,uint32_t length) {
    psmtp_media_player *p=user;
    if(position>=p->size)return 0;
    if(length>p->size-position)length=(uint32_t)(p->size-position);
    if(length>INT32_MAX)return -1;
    uint32_t done=0;
    while(done<length){
        ssize_t n=pread(p->fd,buffer+done,length-done,(off_t)(position+done));
        if(n<0 && errno==EINTR)continue;
        if(n<0)return -1;
        if(!n)break;
        done+=(uint32_t)n;
    }
    return (int)done;
}

#ifndef PSMTP_PRODUCTION
static int media_metadata_read(void *user,uint64_t at,uint8_t *buffer,size_t bytes){
    return file_read(user,buffer,at,(uint32_t)bytes)==(int)bytes?0:MTP_IO;
}

static void log_audio_codec(psmtp_media_player *p,const char *name){
    const char *suffix=strrchr(name,'.');
    if(!suffix || (strcasecmp(suffix,".mp4") && strcasecmp(suffix,".m4v") && strcasecmp(suffix,".mov")))return;
    psmtp_meta_reader reader={.user=p,.size=p->size,.read=media_metadata_read};
    uint32_t codec;
    int rc=psmtp_metadata_mp4_audio_codec(&reader,&codec);
    if(rc){psmtp_lifecycle_mark("media-mp4-audio-codec-unavailable",rc);return;}
    psmtp_lifecycle_mark("media-mp4-audio-fourcc",(int32_t)codec);
}
#endif

static void av_event(void *user,int event,int source,void *details){
    psmtp_media_player *p=user;(void)source;(void)details;
    if(event==2)__atomic_store_n(&p->ready,1,__ATOMIC_RELEASE);
}

static void load_audio_decoders(psmtp_media_player *p){
    for(unsigned i=0;i<sizeof(decoder_modules)/sizeof(*decoder_modules);++i){
        enum OrbisSysModule module=decoder_modules[i];
        if(!sceSysmoduleIsLoaded(module))continue;
        int rc=sceSysmoduleLoadModule(module);
        if(!rc)p->owned_decoders|=1u<<i;
        psmtp_lifecycle_mark("media-decoder-module",module);
        psmtp_lifecycle_mark("media-decoder-load",rc);
    }
}

static int open_audio(unsigned block){
    int32_t rc=sceAudioOutInit();
    if(rc && (uint32_t)rc!=UINT32_C(0x8026000e))return rc;
    int port=sceAudioOutOpen(ORBIS_USER_SERVICE_USER_ID_SYSTEM,ORBIS_AUDIO_OUT_PORT_TYPE_MAIN,0,
        block,48000,ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
    if(port<0){
        int32_t user;
        if(!sceUserServiceGetInitialUser(&user) && user>=0)
            port=sceAudioOutOpen(user,ORBIS_AUDIO_OUT_PORT_TYPE_MAIN,0,block,48000,ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
    }
    if(port<0)return port;
    int32_t volume[8]={32768,32768,32768,32768,32768,32768,32768,32768};
    rc=sceAudioOutSetVolume(port,3,volume);
    if(rc<0){
        sceAudioOutClose(port);
        return rc;
    }
    return port;
}

static int audio_paused(psmtp_media_player *p){
    return __atomic_load_n(&p->paused,__ATOMIC_ACQUIRE) || __atomic_load_n(&p->system_paused,__ATOMIC_ACQUIRE);
}

static int output_audio(int port,const int16_t *pcm){
    int rc=sceAudioOutOutput(port,pcm);
    /* Keep the PCM grain intact until the native audio port releases it. */
    return rc<0?rc:sceAudioOutOutput(port,NULL);
}

/* AudioOut consumes fixed 48 kHz blocks. Convert mono and other sample
 * rates here, outside the controller loop. Retain the native stereo samples
 * unchanged at 48 kHz and preserve fractional resampling phase across frames. */
static void *audio_loop(void *context) {
    psmtp_media_player *p=context;
    int port=-1,error=0;
    enum {BLOCK=256,MAX_AUDIO_BYTES=131072};
    _Alignas(64) int16_t pcm[BLOCK*2];
    int16_t *samples=malloc(MAX_AUDIO_BYTES);
    psmtp_mp4_aac *aac=NULL;
    unsigned decoder_epoch=0;
    unsigned used=0,phase=0,epoch=0,prior_rate=0;
    int previous_left=0,previous_right=0,history=0;
    if(!samples)error=1;
    while(!error && !psmtp_lifecycle_requested() && !__atomic_load_n(&p->stop,__ATOMIC_ACQUIRE)){
        av_audio_frame frame={0};
        unsigned current_epoch=__atomic_load_n(&p->seek_epoch,__ATOMIC_ACQUIRE);
        if(epoch!=current_epoch){epoch=current_epoch;used=phase=0;history=0;}
        if(audio_paused(p)){
            used=phase=0;history=0;sceKernelUsleep(4000);continue;
        }
        if(psmtp_sync_lock(&p->gate)){error=1;break;}
        int available,eof;
        if(p->music){
            int n=p->music_eof?0:psmtp_music_read(p->music,samples,1024);
            if(n<=0)p->music_eof=1;
            eof=p->music_eof;
            if(n<0)error=1;
            available=n>0;
            if(available){
                p->music_position+=(uint64_t)n;
                frame.data=(const uint8_t *)samples;frame.audio.rate=p->music_info.rate;
                frame.audio.channels=(uint16_t)p->music_info.channels;
                frame.audio.bytes=(uint32_t)n*p->music_info.channels*sizeof(*samples);
            }
            psmtp_sync_unlock(&p->gate);
        }else if(p->software_audio && p->start_sent){
            int active=p->active(p->handle);
            uint64_t clock=active?p->current_time(p->handle):0;
            if(active && !clock && p->have_video_pts)clock=p->video_pts;
            unsigned generation=__atomic_load_n(&p->seek_epoch,__ATOMIC_ACQUIRE);
            psmtp_sync_unlock(&p->gate);
            if(!aac){
                error=psmtp_mp4_aac_open(&aac,file_read,p,p->size);
                psmtp_lifecycle_mark("media-software-aac-open",error);
                if(error)break;
                continue;
            }
            if(!active){sceKernelUsleep(4000);continue;}
            uint64_t next=psmtp_mp4_aac_next_ms(aac);
            if(generation!=decoder_epoch || (clock>next && clock-next>250)){
                psmtp_mp4_aac_seek(aac,clock);
                decoder_epoch=generation;
                next=psmtp_mp4_aac_next_ms(aac);
                used=phase=0;history=0;
            }
            if(next!=UINT64_MAX && next>clock && next-clock>20){sceKernelUsleep(3000);continue;}
            unsigned bytes=0,rate=0,channels=0;
            int decoded=psmtp_mp4_aac_read(aac,samples,MAX_AUDIO_BYTES,&bytes,&rate,&channels);
            if(decoded<0){error=decoded;break;}
            if(generation!=__atomic_load_n(&p->seek_epoch,__ATOMIC_ACQUIRE) || audio_paused(p))continue;
            frame.data=(const uint8_t *)samples;
            frame.audio.rate=rate;frame.audio.channels=(uint16_t)channels;frame.audio.bytes=bytes;
            available=bytes>0;eof=!decoded;
        }else {
            available=p->start_sent && p->audio_enabled && p->audio(p->handle,&frame);
            if(available && frame.data && frame.audio.bytes<=MAX_AUDIO_BYTES)memcpy(samples,frame.data,frame.audio.bytes);
            eof=!available && p->start_sent && !p->active(p->handle);
            psmtp_sync_unlock(&p->gate);
        }
        if(error)break;
        if(!available){
            if(port>=0 && used && eof){
                memset(pcm+used*2,0,(BLOCK-used)*2*sizeof(int16_t));
                error=output_audio(port,pcm);
                if(error<0)break;
                error=0;
                used=0;
            }
            sceKernelUsleep(2500);continue;
        }
        unsigned channels=frame.audio.channels,rate=frame.audio.rate;
        if(!frame.data || !channels || channels>8 || rate<8000 || rate>192000 ||
           frame.audio.bytes>MAX_AUDIO_BYTES || frame.audio.bytes%(channels*sizeof(*samples))){error=MTP_PROTOCOL;break;}
        if(prior_rate!=rate){prior_rate=rate;phase=0;history=0;}
        if(port<0){
            port=open_audio(BLOCK);
            psmtp_lifecycle_mark("media-audio-port",port);
            if(port<0){
                error=port;
                break;
            }
        }
        unsigned frames=frame.audio.bytes/(channels*sizeof(*samples));
        for(unsigned i=0;i<frames;++i){
            int left=samples[i*channels],right=samples[i*channels+(channels>1)];
            if(channels>2){
                int center=samples[i*channels+2]/2;
                left=(left+center)/2;right=(right+center)/2;
                for(unsigned c=3;c<channels;++c){
                    int component=samples[i*channels+c]/(c==3?8:4);
                    if(c==3 || !(c&1))left+=component;
                    if(c==3 || (c&1))right+=component;
                }
                if(left>INT16_MAX)left=INT16_MAX;if(left<INT16_MIN)left=INT16_MIN;
                if(right>INT16_MAX)right=INT16_MAX;if(right<INT16_MIN)right=INT16_MIN;
            }
            if(!history){previous_left=left;previous_right=right;history=1;}
            phase+=48000;
            while(phase>=rate){
                phase-=rate;
                int volume=(int)__atomic_load_n(&p->volume,__ATOMIC_RELAXED);
                int scaled_left=left-(int)(((int64_t)left-previous_left)*phase/48000);
                int scaled_right=right-(int)(((int64_t)right-previous_right)*phase/48000);
                pcm[2*used]=(int16_t)(scaled_left*volume/100);
                pcm[2*used+1]=(int16_t)(scaled_right*volume/100);
                if(++used==BLOCK){
                    used=0;
                    error=output_audio(port,pcm);
                    if(error<0)break;
                    error=0;
                    if(!__atomic_exchange_n(&p->got_audio,1,__ATOMIC_ACQ_REL))
                        psmtp_lifecycle_mark("media-first-audio-output",0);
                    if(__atomic_load_n(&p->stop,__ATOMIC_ACQUIRE) || audio_paused(p) ||
                       epoch!=__atomic_load_n(&p->seek_epoch,__ATOMIC_ACQUIRE))break;
                }
            }
            previous_left=left;previous_right=right;
            if(error || __atomic_load_n(&p->stop,__ATOMIC_RELAXED) || audio_paused(p) ||
               epoch!=__atomic_load_n(&p->seek_epoch,__ATOMIC_RELAXED))break;
        }
        if(error)break;
    }
    if(port>=0){
        int rc=sceAudioOutClose(port);
        if(rc<0 && !error)error=rc;
    }
    psmtp_mp4_aac_close(aac);
    free(samples);
    if(error){
        __atomic_store_n(&p->audio_error,error,__ATOMIC_RELEASE);
        psmtp_lifecycle_mark("media-audio-output-error",error);
    }
    return NULL;
}

int psmtp_media_open(psmtp_media_player **out,const psmtp_viewer *v) {
    psmtp_media_player *p=calloc(1,sizeof(*p));
    *out=NULL;
    if(!p)return MTP_MEMORY;
    p->fd=-1;p->volume=p->previous_volume=100;
    p->shown=p->pending=-1;
    p->needs_video=v->kind==PSMTP_FILE_VIDEO;
    const char *path=v->client?v->staged_path:v->source.path;
    int rc=v->client?0:psmtp_op_open_read(&v->source,&p->fd);
    if(v->client){p->fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);if(p->fd<0)rc=MTP_FILESYSTEM;}
    psmtp_native_stat st;
    if(!rc)rc=psmtp_native_status(p->fd,&st);
    if(rc || st.st_size<=0){if(!rc)rc=MTP_PROTOCOL;goto fail;}
    p->size=(uint64_t)st.st_size;
#ifndef PSMTP_PRODUCTION
    if(p->needs_video)log_audio_codec(p,v->source.name);
#endif
    if(v->kind==PSMTP_FILE_MUSIC){
        rc=psmtp_music_open(&p->music,v->source.name,file_read,p,p->size,&p->music_info);
        if(!rc){p->duration=p->music_info.frames/p->music_info.rate*1000+
            p->music_info.frames%p->music_info.rate*1000/p->music_info.rate;goto start_audio;}
        if(rc!=MTP_UNSUPPORTED)goto fail;
    }
    rc=MTP_UNSUPPORTED;
    load_audio_decoders(p);
    if(sceSysmoduleIsLoaded(ORBIS_SYSMODULE_AV_PLAYER)){
        if(sceSysmoduleLoadModule(ORBIS_SYSMODULE_AV_PLAYER))goto fail;
        p->owned=1;
    }
    int module=psmtp_optional_module("libSceAvPlayer");
#define AV(field,name) psmtp_optional_symbol(module,"sceAvPlayer" name,&p->field,sizeof(p->field))
    /* The basic video result has the same prefix. The zero-initialized
     * extension falls back to the basic frame width and uncropped NV12. */
    if(AV(init,"Init") || AV(add,"AddSource") || AV(close,"Close") ||
       (AV(video,"GetVideoDataEx") && AV(video,"GetVideoData")) || AV(enable_stream,"EnableStream") ||
       AV(audio,"GetAudioData") || AV(active,"IsActive") || AV(current_time,"CurrentTime") ||
       AV(pause,"Pause") || AV(resume,"Resume") || AV(start,"Start") || AV(stop_native,"Stop") || AV(jump,"JumpToTime") ||
       AV(stream_count,"StreamCount") || AV(stream_info,"GetStreamInfo"))goto fail;
#undef AV
    int32_t priority;
    if(scePthreadGetprio(scePthreadSelf(),&priority) || priority<ORBIS_KERNEL_PRIO_FIFO_HIGHEST ||
       priority>ORBIS_KERNEL_PRIO_FIFO_LOWEST)priority=ORBIS_KERNEL_PRIO_FIFO_NORMAL;
    p->config=(av_init){.allocate=av_alloc,.release=av_free,.allocate_texture=texture_alloc,.release_texture=texture_free,
        .file_context=p,.open_file=file_open,.close_file=file_close,.read_file=file_read,.size_file=file_size,
        .event_context=p,.event=av_event,.priority=(uint32_t)priority,.buffers=6,.auto_start=false};
    p->handle=p->init(&p->config);
    if(!p->handle)goto fail;
    int (*post_init)(void *,const av_post_init *);
    if(!psmtp_optional_symbol(module,"sceAvPlayerPostInit",&post_init,sizeof(post_init))){
        const av_post_init settings={0};
        psmtp_lifecycle_mark("media-post-init",post_init(p->handle,&settings));
    }
    if(p->add(p->handle,path)<0)goto fail;
start_audio:
    rc=MTP_IO;
    if(psmtp_sync_init(&p->gate))goto fail;
    if(scePthreadCreate(&p->thread,NULL,audio_loop,p,"psmtp-media-audio"))goto fail;
    p->thread_started=1;
    if(p->needs_video){
        if(scePthreadCreate(&p->video_thread,NULL,video_loop,p,"psmtp-media-video"))goto fail;
        p->video_started=1;
    }
    p->opened=media_now();*out=p;
    return 0;
fail:
    if(psmtp_media_close(p))*out=p;
    return rc;
}

static unsigned channel(int value){return value<0?0u:value>255?255u:(unsigned)value;}

static int start_streams(psmtp_media_player *p){
    int count=p->stream_count(p->handle),enabled[2]={0},has_audio=0;
    psmtp_lifecycle_mark("media-stream-count",count);
    for(int i=0;i<count && i<32;++i){
        av_stream stream={0};
        int status=p->stream_info(p->handle,(uint32_t)i,&stream);
        psmtp_lifecycle_mark(status<0?"media-stream-info-error":"media-stream-type",status<0?status:(int)stream.type);
        if(status<0 || stream.type>1 || enabled[stream.type])continue;
        if(!p->needs_video && stream.type==0)continue;
        if(stream.type==1){
            has_audio=1;
            psmtp_lifecycle_mark("media-audio-channels",stream.audio.channels);
            psmtp_lifecycle_mark("media-audio-sample-rate",(int)stream.audio.rate);
        }
        status=p->enable_stream(p->handle,(uint32_t)i);
        psmtp_lifecycle_mark(stream.type==1?"media-enable-audio":"media-enable-video",status);
        if(status<0)continue;
        enabled[stream.type]=1;
        if(stream.duration>p->duration)p->duration=stream.duration;
    }
    if(!enabled[p->needs_video?0:1] || p->start(p->handle)<0)return MTP_UNSUPPORTED;
    p->audio_enabled=enabled[1];
    p->software_audio=p->needs_video && has_audio && !p->audio_enabled;
    psmtp_lifecycle_mark("media-audio-stream",p->audio_enabled?1:p->software_audio?2:0);
    p->start_sent=1;
    return 0;
}
static size_t video_bytes(const av_video_frame *frame){
    av_video v=frame->video;
    unsigned pitch=v.pitch?v.pitch:v.width;
    if(!frame->data || !v.width || !v.height || v.width>4096 || v.height>2304 ||
        pitch<((v.width+1)&~1u) || pitch>8192 || v.luma_depth>8 || v.chroma_depth>8 ||
        v.left>=v.width || v.right>=v.width-v.left || v.top>=v.height || v.bottom>=v.height-v.top)return 0;
    return (size_t)pitch*(v.height+(v.height+1)/2);
}
static int convert_frame(video_surface *surface,const av_video_frame *frame) {
    av_video v=frame->video;
    unsigned pitch=v.pitch?v.pitch:v.width;
    unsigned width=v.width-v.left-v.right,height=v.height-v.top-v.bottom;
    surface->aspect=v.aspect>=0.25f && v.aspect<=8.0f?v.aspect:(float)width/height;
    if(surface->width!=width || surface->height!=height){
        uint32_t *pixels=realloc(surface->pixels,(size_t)width*height*sizeof(*pixels));
        if(!pixels)return MTP_MEMORY;
        surface->pixels=pixels;surface->width=width;surface->height=height;
    }
    const uint8_t *chroma=frame->data+(size_t)pitch*v.height;
    int hd=height>=720;
    int yr=v.full_range?256:298,rv=hd?459:409,gu=hd?55:100,gv=hd?136:208,bu=hd?541:516;
    if(v.full_range){rv=hd?403:359;gu=hd?48:88;gv=hd?120:183;bu=hd?475:454;}
    for(unsigned y=0;y<height;++y){
        unsigned sy=y+v.top;
        const uint8_t *luma=frame->data+(size_t)sy*pitch;
        const uint8_t *uv=chroma+(size_t)(sy/2)*pitch;
        uint32_t *out=surface->pixels+(size_t)y*width;
        for(unsigned x=0;x<width;++x){
            unsigned sx=x+v.left,uv_x=sx&~1u;
            int u=uv[uv_x]-128,z=uv[uv_x+1]-128;
            int l=yr*((int)luma[sx]-(v.full_range?0:16))+128;
            out[x]=0xff000000u|channel((l+rv*z)>>8)|channel((l-gu*u-gv*z)>>8)<<8|channel((l+bu*u)>>8)<<16;
        }
    }
    return 0;
}

/* AvPlayer schedules decoded frames against its playback clock. Poll it
 * independently of rendering and let the presenter take the newest frame.
 * A third surface lets conversion proceed while one frame is displayed and
 * another is waiting for the next flip. */
static void *video_loop(void *context){
    psmtp_media_player *p=context;
    uint8_t *nv12=NULL;
    size_t capacity=0;
    int rc=0;
    while(!psmtp_lifecycle_requested() && !__atomic_load_n(&p->stop,__ATOMIC_ACQUIRE)){
        av_video_frame frame={0};
        if(psmtp_sync_lock(&p->gate)){rc=MTP_IO;break;}
        if(audio_paused(p) || !p->start_sent || !p->video(p->handle,&frame) ||
            (p->have_video_pts && frame.time<=p->video_pts)){
            psmtp_sync_unlock(&p->gate);
            sceKernelUsleep(1000);
            continue;
        }
        size_t bytes=video_bytes(&frame);
        if(!bytes){psmtp_sync_unlock(&p->gate);rc=MTP_UNSUPPORTED;break;}
        if(bytes>capacity){
            uint8_t *buffer=realloc(nv12,bytes);
            if(!buffer){psmtp_sync_unlock(&p->gate);rc=MTP_MEMORY;break;}
            nv12=buffer;capacity=bytes;
        }
        /* Seek and pause can invalidate the decoder's surface. Copy it while
         * holding the native gate, then release audio for color conversion. */
        memcpy(nv12,frame.data,bytes);
        frame.data=nv12;
        unsigned epoch=__atomic_load_n(&p->seek_epoch,__ATOMIC_RELAXED);
        int back=0;
        while(back==p->shown || back==p->pending)++back;
        p->video_pts=frame.time;p->have_video_pts=1;p->video_processing=1;
        psmtp_sync_unlock(&p->gate);
        rc=convert_frame(&p->surfaces[back],&frame);
        if(psmtp_sync_lock(&p->gate)){rc=MTP_IO;break;}
        p->video_processing=0;
        if(!rc && epoch==__atomic_load_n(&p->seek_epoch,__ATOMIC_RELAXED))p->pending=back;
        psmtp_sync_unlock(&p->gate);
        if(rc)break;
    }
    free(nv12);
    if(rc)__atomic_store_n(&p->video_error,rc,__ATOMIC_RELEASE);
    return NULL;
}

int psmtp_media_update(psmtp_media_player *p,psmtp_media_frame *out) {
    int video_error=__atomic_load_n(&p->video_error,__ATOMIC_ACQUIRE);
    if(video_error)return video_error;
    if(psmtp_sync_lock(&p->gate))return MTP_IO;
    if(p->music){
        uint64_t pos=p->music_position;
        p->time=pos/p->music_info.rate*1000+pos%p->music_info.rate*1000/p->music_info.rate;
        p->ended=p->music_eof;
        psmtp_sync_unlock(&p->gate);
        int waiting=!pos && !p->ended && !__atomic_load_n(&p->audio_error,__ATOMIC_ACQUIRE);
        *out=(psmtp_media_frame){.volume=__atomic_load_n(&p->volume,__ATOMIC_RELAXED),.time=p->time,.duration=p->duration,.loading=waiting,
            .paused=__atomic_load_n(&p->paused,__ATOMIC_ACQUIRE),.ended=p->ended,.audio_error=__atomic_load_n(&p->audio_error,__ATOMIC_ACQUIRE)};
        return 0;
    }
    int suspended=__atomic_load_n(&p->system_paused,__ATOMIC_ACQUIRE);
    if(!suspended && !p->start_sent && __atomic_load_n(&p->ready,__ATOMIC_ACQUIRE)){
        int rc=start_streams(p);
        if(rc){psmtp_sync_unlock(&p->gate);return rc;}
    }
    int paused=audio_paused(p);
    if(!paused && p->pending>=0){
        p->shown=p->pending;p->pending=-1;++p->sequence;
    }
    int active=p->start_sent && p->active(p->handle);
    uint64_t clock=active?p->current_time(p->handle):0;
    int video_processing=p->video_processing;
    video_surface surface=p->shown<0?(video_surface){0}:p->surfaces[p->shown];
    psmtp_sync_unlock(&p->gate);
    int started=p->needs_video?surface.pixels!=NULL:__atomic_load_n(&p->got_audio,__ATOMIC_ACQUIRE);
    int audio_error=__atomic_load_n(&p->audio_error,__ATOMIC_ACQUIRE);
    if(!paused && clock>p->time)p->time=clock;
    if(started && !active && !paused && !video_processing){p->ended=1;if(p->duration)p->time=p->duration;}
    if(!started && !paused && media_now()-p->opened>15000)return audio_error?MTP_IO:MTP_UNSUPPORTED;
    *out=(psmtp_media_frame){surface.pixels,surface.width,surface.height,__atomic_load_n(&p->volume,__ATOMIC_RELAXED),surface.aspect,p->time,p->duration,
        paused,p->ended,!started,audio_error,p->sequence};
    return 0;
}

int psmtp_media_pause(psmtp_media_player *p) {
    int paused=__atomic_load_n(&p->paused,__ATOMIC_ACQUIRE),rc;
    if(p->ended)return psmtp_media_seek(p,0);
    if(psmtp_sync_lock(&p->gate))return MTP_IO;
    rc=p->music?0:paused?p->resume(p->handle):p->pause(p->handle);
    if(rc>=0)__atomic_store_n(&p->paused,!paused,__ATOMIC_RELEASE);
    psmtp_sync_unlock(&p->gate);
    return rc<0?MTP_UNSUPPORTED:0;
}

int psmtp_media_seek(psmtp_media_player *p,int delta) {
    uint64_t target=0;
    if(delta<0)target=p->time>(uint64_t)-delta?p->time-(uint64_t)-delta:0;
    if(delta>0)target=p->time+(uint64_t)delta;
    if(p->duration && target>=p->duration)target=p->duration-1;
    if(psmtp_sync_lock(&p->gate))return MTP_IO;
    int rc;
    if(p->music){
        uint64_t frame=target/1000*p->music_info.rate+target%1000*p->music_info.rate/1000;
        rc=psmtp_music_seek(p->music,frame);
        if(!rc){p->music_position=frame;p->music_eof=p->ended=0;}
    }else rc=p->jump(p->handle,target);
    if(rc>=0 && p->ended){
        p->opened=media_now();
        rc=p->start(p->handle);
        if(rc>=0)p->ended=0;
    }
    if(rc>=0){
        p->time=target;
        p->have_video_pts=0;
        p->pending=-1;
        __atomic_add_fetch(&p->seek_epoch,1,__ATOMIC_RELEASE);
    }
    psmtp_sync_unlock(&p->gate);
    return rc<0?MTP_UNSUPPORTED:0;
}

int psmtp_media_system_pause(psmtp_media_player *p,int suspended){
    int previous=__atomic_load_n(&p->system_paused,__ATOMIC_ACQUIRE);
    if(previous==suspended)return 0;
    if(psmtp_sync_lock(&p->gate))return MTP_IO;
    int rc=0;
    if(p->start_sent && !p->ended && !__atomic_load_n(&p->paused,__ATOMIC_ACQUIRE))
        rc=suspended?p->pause(p->handle):p->resume(p->handle);
    if(rc>=0){
        __atomic_store_n(&p->system_paused,suspended,__ATOMIC_RELEASE);
        if(!suspended && !p->sequence)p->opened=media_now();
    }
    psmtp_sync_unlock(&p->gate);
    return rc<0?MTP_IO:0;
}

void psmtp_media_volume(psmtp_media_player *p,int delta) {
    int volume=(int)__atomic_load_n(&p->volume,__ATOMIC_RELAXED);
    if(!delta && volume){p->previous_volume=(unsigned)volume;volume=0;}
    else volume=delta?volume+delta:(int)p->previous_volume;
    __atomic_store_n(&p->volume,(unsigned)(volume<0?0:volume>100?100:volume),__ATOMIC_RELAXED);
}

static void *release_player(void *context){
    psmtp_media_player *p=context;
    int rc=0;
    if(p->thread_started){
        rc=scePthreadJoin(p->thread,NULL);
        if(rc)goto done;
    }
    if(p->video_started){
        rc=scePthreadJoin(p->video_thread,NULL);
        if(rc)goto done;
    }
    if(p->handle){
        if(p->start_sent)(void)p->stop_native(p->handle);
        rc=p->close(p->handle);
        if(rc<0)goto done;
    }
    psmtp_music_close(p->music);
    psmtp_sync_destroy(&p->gate);
    if(p->owned)sceSysmoduleUnloadModule(ORBIS_SYSMODULE_AV_PLAYER);
    for(unsigned i=sizeof(decoder_modules)/sizeof(*decoder_modules);i>0;--i)
        if(p->owned_decoders&(1u<<(i-1)))sceSysmoduleUnloadModule(decoder_modules[i-1]);
    if(p->fd>=0)close(p->fd);
    for(unsigned i=0;i<sizeof(p->surfaces)/sizeof(*p->surfaces);++i)free(p->surfaces[i].pixels);
done:
    p->close_error=rc;
    __atomic_store_n(&p->close_done,1,__ATOMIC_RELEASE);
    return NULL;
}

int psmtp_media_close_poll(psmtp_media_player *p,unsigned wait_ms) {
    if(!p)return 0;
    __atomic_store_n(&p->stop,1,__ATOMIC_RELEASE);
    if(!p->closing){
        if(scePthreadCreate(&p->close_thread,NULL,release_player,p,"psmtp-media-close"))return MTP_IO;
        p->closing=1;
    }
    for(unsigned elapsed=0;elapsed<wait_ms && !__atomic_load_n(&p->close_done,__ATOMIC_ACQUIRE);elapsed+=5)sceKernelUsleep(5000);
    if(!__atomic_load_n(&p->close_done,__ATOMIC_ACQUIRE))return MTP_TIMEOUT;
    if(!p->close_joined){
        if(psmtp_lifecycle_join((void *)p->close_thread,NULL,100))return MTP_TIMEOUT;
        p->close_joined=1;
    }
    if(p->close_error)return MTP_IO;
    free(p);
    return 0;
}
int psmtp_media_close(psmtp_media_player *p){return psmtp_media_close_poll(p,800);}
