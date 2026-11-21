/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#include "mp4_aac.h"
#include "psmtp/mtp.h"
#include <neaacdec.h>
#include <stdlib.h>
#include <string.h>

/* Read only the movie index and the selected track's individual packets.
 * All offsets stay relative to the already-open, size-checked media source. */
typedef struct {const uint8_t *p; size_t n;} span;
typedef struct {uint64_t offset, time; uint32_t bytes;} packet;
struct psmtp_mp4_aac {
    psmtp_music_read_at read;
    void *user;
    NeAACDecHandle handle;
    packet *packets;
    uint8_t *encoded;
    uint32_t count, next, scale;
    uint64_t origin, delay_ms;
};

static uint32_t u32(const uint8_t *p){
    return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];
}
static uint64_t u64(const uint8_t *p){return (uint64_t)u32(p)<<32 | u32(p+4);}
static span skip(span s,size_t n){return s.p && n<=s.n?(span){s.p+n,s.n-n}:(span){0};}
static span next_box(span *rest,const char *type){
    while(rest->n>=8){
        const uint8_t *header=rest->p;
        size_t prefix=8;
        uint64_t n=u32(header);
        if(n==1 && rest->n>=16){n=u64(header+8);prefix=16;}
        else if(!n)n=rest->n;
        if(n<prefix || n>rest->n)break;
        *rest=skip(*rest,(size_t)n);
        if(!memcmp(header+4,type,4))return (span){header+prefix,(size_t)n-prefix};
    }
    *rest=(span){0};
    return (span){0};
}
static span box(span parent,const char *type){return next_box(&parent,type);}

/* ISO 14496 descriptor lengths have up to four base-128 digits. */
static span descriptor(span s,unsigned tag){
    if(s.n<2 || *s.p!=tag)return (span){0};
    uint32_t length=0;
    for(unsigned i=1;i<=4 && i<s.n;++i){
        length=(length<<7)|(s.p[i]&127);
        if(!(s.p[i]&128) && length<=s.n-i-1)return (span){s.p+i+1,length};
        if(!(s.p[i]&128))break;
    }
    return (span){0};
}
static span audio_config(span table){
    span stsd=box(table,"stsd");
    if(stsd.n<8 || u32(stsd.p+4)!=1)return (span){0};
    span entry=box(skip(stsd,8),"mp4a");
    if(entry.n<28 || entry.p[8] || entry.p[9])return (span){0};
    span es=descriptor(skip(box(skip(entry,28),"esds"),4),3);
    if(es.n<3)return (span){0};
    uint8_t flags=es.p[2];
    es=skip(es,3+(flags&128?2:0));
    if(flags&64){if(!es.n)return (span){0};es=skip(es,1u+es.p[0]);}
    if(flags&32)es=skip(es,2);
    span decoder=descriptor(es,4);
    if(decoder.n<13 || (decoder.p[0]!=0x40 && (decoder.p[0]<0x66 || decoder.p[0]>0x68)))return (span){0};
    return descriptor(skip(decoder,13),5);
}

static uint32_t timescale(span header){
    if(!header.n || header.p[0]>1)return 0;
    size_t offset=header.p[0]?20:12;
    return header.n>=offset+4?u32(header.p+offset):0;
}

/* Apply the usual track edit (encoder priming) and optional initial silence.
 * More elaborate edit sequences require a presentation timeline of their own. */
static int track_edit(psmtp_mp4_aac *a,span track,uint32_t movie_scale){
    span edit=box(box(track,"edts"),"elst");
    if(!edit.p)return 0;
    if(edit.n<8 || edit.p[0]>1 || !movie_scale)return MTP_UNSUPPORTED;
    int wide=edit.p[0];
    size_t stride=wide?20:12;
    uint32_t count=u32(edit.p+4);
    if(count>2 || count>(edit.n-8)/stride)return MTP_UNSUPPORTED;
    for(unsigned i=0;i<count;++i){
        const uint8_t *p=edit.p+8+i*stride;
        uint64_t duration=wide?u64(p):u32(p);
        int64_t origin=wide?(int64_t)u64(p+8):(int32_t)u32(p+4);
        if(u32(p+stride-4)!=0x10000)return MTP_UNSUPPORTED;
        if(i==0 && origin==-1 && count==2 && duration<=UINT64_MAX/1000)
            {a->delay_ms=duration*1000/movie_scale;if(a->delay_ms>UINT32_MAX)return MTP_LIMIT;}
        else if(origin>=0 && i==count-1)a->origin=(uint64_t)origin;
        else return MTP_UNSUPPORTED;
    }
    return 0;
}

static int entries(span s,unsigned width,uint32_t *count){
    if(s.n<8)return 0;
    *count=u32(s.p+4);
    return *count && *count<=(s.n-8)/width;
}
static int index_packets(psmtp_mp4_aac *a,span table,uint64_t file_size){
    span sizes=box(table,"stsz"),mapping=box(table,"stsc"),clock=box(table,"stts");
    span offsets=box(table,"stco");
    unsigned width=4;
    if(!offsets.p){offsets=box(table,"co64");width=8;}
    uint32_t chunks,runs,times;
    if(sizes.n<12 || !entries(offsets,width,&chunks) || !entries(mapping,12,&runs) ||
       !entries(clock,8,&times) || u32(mapping.p+8)!=1)return MTP_PROTOCOL;
    uint32_t fixed=u32(sizes.p+4),count=u32(sizes.p+8);
    if(!count || count>1000000 || (!fixed && count>(sizes.n-12)/4))return MTP_LIMIT;
    a->packets=calloc(count,sizeof(packet));
    if(!a->packets)return MTP_MEMORY;
    uint32_t sample=0,run=0,largest=0;
    for(uint32_t i=0;i<chunks;++i){
        const uint8_t *map=mapping.p+8+12*(size_t)run;
        if(run+1<runs && u32(map+12)==i+1){++run;map+=12;}
        if((run+1<runs && u32(map+12)<=i+1) || u32(map+8)!=1)return MTP_PROTOCOL;
        uint32_t n=u32(map+4);
        const uint8_t *pos=offsets.p+8+(size_t)i*width;
        uint64_t at=width==8?u64(pos):u32(pos);
        if(!n || n>count-sample)return MTP_PROTOCOL;
        for(uint32_t j=0;j<n;++j,++sample){
            uint32_t bytes=fixed?fixed:u32(sizes.p+12+(size_t)sample*4);
            if(!bytes || bytes>65536 || at>file_size || bytes>file_size-at)return MTP_PROTOCOL;
            a->packets[sample].offset=at;
            a->packets[sample].bytes=bytes;
            at+=bytes;
            if(bytes>largest)largest=bytes;
        }
    }
    if(sample!=count || run!=runs-1)return MTP_PROTOCOL;
    sample=0;
    uint64_t ticks=0;
    for(uint32_t i=0;i<times;++i){
        const uint8_t *p=clock.p+8+(size_t)i*8;
        uint32_t n=u32(p),delta=u32(p+4);
        if(!delta || !n || n>count-sample)return MTP_PROTOCOL;
        for(uint32_t j=0;j<n;++j){a->packets[sample++].time=ticks;ticks+=delta;}
    }
    if(sample!=count)return MTP_PROTOCOL;
    a->count=count;
    a->encoded=malloc(largest);
    return a->encoded?0:MTP_MEMORY;
}

static int open_track(psmtp_mp4_aac *a,span movie,uint64_t size){
    uint32_t movie_scale=timescale(box(movie,"mvhd"));
    span track;
    while((track=next_box(&movie,"trak")).p){
        span media=box(track,"mdia"),handler=box(media,"hdlr");
        if(handler.n<12 || memcmp(handler.p+8,"soun",4))continue;
        span table=box(box(media,"minf"),"stbl");
        span config=audio_config(table);
        if(!config.n || config.n>256)continue;
        a->scale=timescale(box(media,"mdhd"));
        if(!a->scale)return MTP_PROTOCOL;
        a->handle=NeAACDecOpen();
        if(!a->handle)return MTP_MEMORY;
        unsigned long rate;
        unsigned char channels;
        if(NeAACDecInit2(a->handle,(unsigned char *)config.p,(unsigned long)config.n,&rate,&channels)!=0){
            NeAACDecClose(a->handle);a->handle=NULL;continue;
        }
        int rc=track_edit(a,track,movie_scale);
        return rc?rc:index_packets(a,table,size);
    }
    return MTP_UNSUPPORTED;
}

int psmtp_mp4_aac_open(psmtp_mp4_aac **out,psmtp_music_read_at read,void *user,uint64_t size){
    *out=NULL;
    uint64_t at=0;
    for(unsigned i=0;i<4096 && size-at>=8;++i){
        uint8_t h[16];
        if(read(user,h,at,8)!=8)return MTP_IO;
        uint64_t n=u32(h),prefix=8;
        if(n==1){
            if(size-at<16 || read(user,h+8,at+8,8)!=8)return MTP_PROTOCOL;
            n=u64(h+8);prefix=16;
        }else if(!n)n=size-at;
        if(n<prefix || n>size-at)return MTP_PROTOCOL;
        if(!memcmp(h+4,"moov",4)){
            uint64_t bytes=n-prefix;
            if(!bytes || bytes>32u*1024*1024)return MTP_LIMIT;
            uint8_t *data=malloc((size_t)bytes);
            psmtp_mp4_aac *a=calloc(1,sizeof(*a));
            if(!data || !a){free(data);free(a);return MTP_MEMORY;}
            a->read=read;a->user=user;
            int rc=read(user,data,at+prefix,(uint32_t)bytes)==(int)bytes?
                open_track(a,(span){data,(size_t)bytes},size):MTP_IO;
            free(data);
            if(rc){psmtp_mp4_aac_close(a);return rc;}
            *out=a;return 0;
        }
        at+=n;
    }
    return MTP_UNSUPPORTED;
}
void psmtp_mp4_aac_close(psmtp_mp4_aac *a){
    if(!a)return;
    if(a->handle)NeAACDecClose(a->handle);
    free(a->packets);free(a->encoded);free(a);
}
static uint64_t packet_ms(const psmtp_mp4_aac *a,uint32_t index){
    uint64_t tick=a->packets[index].time;
    uint64_t ms=tick>a->origin?(tick-a->origin)*1000/a->scale:0;
    return a->delay_ms+ms;
}
uint64_t psmtp_mp4_aac_next_ms(const psmtp_mp4_aac *a){
    return a->next<a->count?packet_ms(a,a->next):UINT64_MAX;
}
void psmtp_mp4_aac_seek(psmtp_mp4_aac *a,uint64_t ms){
    uint32_t lo=0,hi=a->count;
    while(lo<hi){
        uint32_t mid=lo+(hi-lo)/2;
        if(packet_ms(a,mid)<ms)lo=mid+1;else hi=mid;
    }
    a->next=lo?lo-1:0;
    NeAACDecPostSeekReset(a->handle,(long)a->next);
}
int psmtp_mp4_aac_read(psmtp_mp4_aac *a,int16_t *pcm,size_t capacity,
                       unsigned *bytes,unsigned *rate,unsigned *channels){
    *bytes=0;
    if(a->next==a->count)return 0;
    const packet *pkt=&a->packets[a->next++];
    if(a->read(a->user,a->encoded,pkt->offset,pkt->bytes)!=(int)pkt->bytes)return MTP_IO;
    NeAACDecFrameInfo frame={0};
    void *samples=NeAACDecDecode(a->handle,&frame,a->encoded,pkt->bytes);
    if(frame.error || frame.samples>capacity/sizeof(*pcm))return MTP_PROTOCOL;
    if(!frame.samples)return 1; /* decoder priming */
    if(!samples || !frame.channels || frame.channels>8 || frame.samplerate<8000 ||
       frame.samplerate>192000 || frame.samples%frame.channels)return MTP_UNSUPPORTED;
    *bytes=(unsigned)frame.samples*sizeof(*pcm);
    *rate=(unsigned)frame.samplerate;*channels=frame.channels;
    memcpy(pcm,samples,*bytes);
    return 1;
}
