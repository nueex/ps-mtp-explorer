/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#include "psmtp/metadata.h"
#include "psmtp/mtp.h"
#include "psmtp/ui_format.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "preview_decode_budget.h"
#include "image_scale.h"
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wunused-but-set-variable"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_NO_SIMD
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_ONLY_TGA
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_MAX_DIMENSIONS 8192
#define STBI_MALLOC(size) psmtp_decode_malloc(size)
#define STBI_REALLOC_SIZED(p,old_size,size) psmtp_decode_realloc(p,old_size,size)
#define STBI_FREE(p) psmtp_decode_free(p)
/* Upstream dev's mad4 helper guard omits PNG with NO_HDR/NO_LINEAR.
 * Supply its exact upstream helper here for T12's JPEG/PNG-only configuration,
 * retaining the unmodified vendored header and all existing decoder options.
 * Remove when upstream widens the stbi__mad4sizes_valid definition guard.
 * This helper uses the header's checked arithmetic (MIT/public-domain stb).
 */
static int stbi__mul2sizes_valid(int a, int b);
static int stbi__addsizes_valid(int a, int b);
static int stbi__mad4sizes_valid(int a, int b, int c, int d, int add)
{
   return stbi__mul2sizes_valid(a, b) && stbi__mul2sizes_valid(a*b, c) &&
      stbi__mul2sizes_valid(a*b*c, d) && stbi__addsizes_valid(a*b*c*d, add);
}
/* Exact upstream dev snapshot; provenance is in THIRD_PARTY_LICENSES.md. */
#include "../third_party/stb/stb_image.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
static uint32_t be32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static uint64_t be64(const uint8_t *p){return (uint64_t)be32(p)<<32|be32(p+4);}
static uint32_t le32(const uint8_t *p){return (uint32_t)p[3]<<24|(uint32_t)p[2]<<16|(uint32_t)p[1]<<8|p[0];}
static unsigned ex16(const uint8_t *p,int little){return little?p[0]|(unsigned)p[1]<<8:(unsigned)p[0]<<8|p[1];}
static unsigned orientation(const uint8_t *p,size_t n){
    size_t at=2;if(n<4 || p[0]!=255 || p[1]!=216)return 1;
    while(at+4<=n && p[at]==255){unsigned tag=p[at+1],length=(unsigned)p[at+2]*256+p[at+3];if(length<2 || length>n-at-2)break;
        if(tag==225 && length>=16 && !memcmp(p+at+4,"Exif\0\0",6)){const uint8_t *t=p+at+10;size_t size=length-8,off;unsigned count,i;int little=t[0]=='I' && t[1]=='I';if(!little && (t[0]!='M'||t[1]!='M'))return 1;off=little?le32(t+4):be32(t+4);if(off>size || size-off<2)return 1;count=ex16(t+off,little);off+=2;for(i=0;i<count && size-off>=12;++i,off+=12)if(ex16(t+off,little)==0x112 && ex16(t+off+2,little)==3){unsigned v=ex16(t+off+8,little);return v>=1 && v<=8?v:1;}}
        if(tag==218 || tag==217)break;at+=2+length;
    }return 1;
}
static const uint32_t image_fallback[4]={0xff3a3a3a,0xff505050,0xff505050,0xff3a3a3a};
const char *psmtp_metadata_preview_reason(psmtp_preview_reason r){
    switch(r){case PSMTP_PREVIEW_OK:return "ok";case PSMTP_PREVIEW_ENCODED_LIMIT:return "encoded-limit";
    case PSMTP_PREVIEW_DIMENSIONS:return "dimensions";case PSMTP_PREVIEW_ESTIMATE_LIMIT:return "estimate-limit";
    case PSMTP_PREVIEW_ALLOCATION_LIMIT:return "allocation-limit";case PSMTP_PREVIEW_INVALID_IMAGE:return "invalid-image";
    case PSMTP_PREVIEW_SYSTEM_MEMORY:return "system-memory";case PSMTP_PREVIEW_SOURCE_READ:return "source-read";default:return "none";}
}
void psmtp_metadata_image_failed(psmtp_metadata *m,psmtp_preview_reason reason,int error){
    free(m->image);m->image=NULL;m->image_width=m->image_height=0;
    m->fallback_image=image_fallback;m->preview_reason=reason;m->loaded=1;m->error=error;
}
typedef struct {unsigned w,h,n,depth,interlace,progressive,hs[4],vs[4];int png;} image_layout;
static int image_header(const uint8_t *p,size_t n,image_layout *s){
    size_t at;memset(s,0,sizeof(*s));
    if(n>=8 && !memcmp(p,"\x89PNG\r\n\x1a\n",8)){
        unsigned color;if(n<33 || be32(p+8)!=13 || memcmp(p+12,"IHDR",4))return 0;
        s->png=1;s->w=be32(p+16);s->h=be32(p+20);s->depth=p[24];color=p[25];s->interlace=p[28];
        s->n=color==0?1:color==2?3:color==3?1:color==4?2:color==6?4:0;
        if(!s->n || p[26] || p[27] || s->interlace>1)return 0;
        if(s->depth!=1 && s->depth!=2 && s->depth!=4 && s->depth!=8 && s->depth!=16)return 0;
        if((s->depth<8 && color!=0 && color!=3) || (s->depth==16 && color==3))return 0;
        return s->w && s->h;
    }
    if(n<4 || p[0]!=255 || p[1]!=216)return 0;
    at=2;
    while(at<n){
        unsigned marker,length,i;if(p[at++]!=255)return 0;
        while(at<n && p[at]==255)++at;if(at>=n)return 0;marker=p[at++];
        if(marker==0xd8 || marker==1 || (marker>=0xd0 && marker<=0xd7))continue;
        if(marker==0xd9 || marker==0xda || n-at<2)return 0;
        length=(unsigned)p[at]*256+p[at+1];if(length<2 || length>n-at)return 0;
        if(marker==0xc0 || marker==0xc1 || marker==0xc2){
            if(length<11 || p[at+2]!=8)return 0;
            s->h=(unsigned)p[at+3]*256+p[at+4];s->w=(unsigned)p[at+5]*256+p[at+6];s->n=p[at+7];
            s->depth=8;s->progressive=marker==0xc2;
            if((s->n!=1 && s->n!=3 && s->n!=4) || length!=8+3*s->n)return 0;
            for(i=0;i<s->n;++i){unsigned sample=p[at+9+3*i];s->hs[i]=sample>>4;s->vs[i]=sample&15;
                if(!s->hs[i] || s->hs[i]>4 || !s->vs[i] || s->vs[i]>4)return 0;}
            return s->w && s->h;
        }
        at+=length;
    }return 0;
}
static int image_mul(uint64_t a,uint64_t b,uint64_t *out){if(b && a>UINT64_MAX/b)return 0;*out=a*b;return 1;}
/* stb can tolerate missing terminal bytes. Reject incomplete encoded framing
 * before decoding: complete PNG chunks/IEND, or a JPEG EOI after an SOS scan.
 * This does not authenticate pixels or add PNG CRC validation. */
static int image_complete(const uint8_t *p,size_t n,int png){
    size_t at=png?8:2;int seen=0,entropy=0;
    if(png){
        while(at<=n && n-at>=12){uint32_t length=be32(p+at);
            if(length>n-at-12)return 0;
            if(!memcmp(p+at+4,"IDAT",4))seen=1;
            if(!memcmp(p+at+4,"IEND",4))return seen && !length;
            at+=(size_t)length+12;
        }return 0;
    }
    while(at<n){unsigned marker,length;
        if(entropy){while(at<n && p[at]!=255)++at;if(at==n)return 0;}
        if(p[at++]!=255)return 0;while(at<n && p[at]==255)++at;if(at==n)return 0;marker=p[at++];
        if(!marker){if(entropy)continue;return 0;}
        if(marker>=0xd0 && marker<=0xd7){if(!entropy)return 0;continue;}
        if(marker==0xd9)return seen;
        if(marker==1)continue;
        if(marker==0xd8 || n-at<2)return 0;
        length=(unsigned)p[at]*256+p[at+1];if(length<2 || length>n-at)return 0;
        entropy=marker==0xda;if(entropy)seen=1;at+=length;
    }return 0;
}
static int image_add(uint64_t *sum,uint64_t value){if(*sum>UINT64_MAX-value)return 0;*sum+=value;return 1;}
static uint64_t image_max(uint64_t a,uint64_t b){return a>b?a:b;}
/* Bounds normal decoding from the pinned header's buffer layout. A stream
 * expanding beyond its declared PNG dimensions is stopped by the allocator,
 * not trusted merely because its small IHDR passed this estimate. */
static int image_estimate(const image_layout *s,size_t length,uint64_t *bytes){
    uint64_t pixels,peak=0,output;
    if(!image_mul(s->w,s->h,&pixels) || !image_mul(pixels,4,&output))return 0;
    if(s->png){
        uint64_t row,guess,raw,capacity,idat,inflate_peak,creation,conversion,filter;
        if(!image_mul(s->w,s->depth,&row))return 0;
        row=(row+7)/8;
        if(!image_mul(row,s->n,&guess) || !image_add(&guess,1) || !image_mul(guess,s->h,&guess))return 0;
        raw=guess;
        if(s->interlace){
            static const unsigned xo[7]={0,4,0,2,0,1,0},yo[7]={0,0,4,0,2,0,1};
            static const unsigned xs[7]={8,8,4,4,2,2,1},ys[7]={8,8,8,4,4,2,2};unsigned pass;
            raw=0;
            for(pass=0;pass<7;++pass)if(s->w>xo[pass] && s->h>yo[pass]){
                uint64_t w=(s->w-xo[pass]+xs[pass]-1)/xs[pass],h=(s->h-yo[pass]+ys[pass]-1)/ys[pass],part;
                if(!image_mul(w,s->n*s->depth,&part))return 0;part=(part+7)/8;
                if(!image_add(&part,1) || !image_mul(part,h,&part) || !image_add(&raw,part))return 0;
            }
        }
        capacity=guess;while(capacity<raw){if(capacity>UINT64_MAX/2)return 0;capacity*=2;}
        if(!image_mul(length,2,&idat))return 0;idat=image_max(idat,4096);
        inflate_peak=capacity;if(capacity>guess && !image_add(&inflate_peak,capacity/2))return 0;
        peak=idat+idat/2; /* realloc may temporarily retain the old IDAT buffer */
        if(!image_add(&inflate_peak,idat))return 0;peak=image_max(peak,inflate_peak);
        if(!image_mul(output,s->depth==16?2:1,&creation))return 0;
        if(s->interlace && !image_add(&creation,creation/2))return 0; /* largest Adam7 pass <= half the pixels */
        if(!image_mul(s->w,s->n*s->depth,&filter))return 0;filter=2*((filter+7)/8);
        if(!image_add(&creation,capacity) || !image_add(&creation,filter))return 0;
        peak=image_max(peak,creation);
        if(!image_mul(pixels,s->depth==16?12:8,&conversion))return 0;peak=image_max(peak,conversion);
    }else{
        unsigned i,hm=1,vm=1;uint64_t mx,my;
        for(i=0;i<s->n;++i){if(s->hs[i]>hm)hm=s->hs[i];if(s->vs[i]>vm)vm=s->vs[i];}
        mx=((uint64_t)s->w+hm*8-1)/(hm*8);my=((uint64_t)s->h+vm*8-1)/(vm*8);
        peak=output;
        for(i=0;i<s->n;++i){uint64_t plane;
            if(hm%s->hs[i] || vm%s->vs[i])return 0;
            if(!image_mul(mx*s->hs[i]*8,my*s->vs[i]*8,&plane) ||
               !image_mul(plane,s->progressive?3:1,&plane) || !image_add(&peak,plane) ||
               !image_add(&peak,(uint64_t)s->w+3+32))return 0;
        }
    }
    /* Includes decoder state, alignment slop and allocation headers. Encoded
     * bytes, context and thumbnail space are reserved separately below. */
    if(!image_add(&peak,65536))return 0;*bytes=peak;return 1;
}
void psmtp_metadata_free(psmtp_metadata *m){free(m->image);memset(m,0,sizeof(*m));}
int psmtp_decode_still(const uint8_t *data,size_t length,uint32_t **pixels,unsigned *width,unsigned *height){
    int w,h,channels,rc=MTP_PROTOCOL;
    *pixels=NULL;*width=*height=0;
    if(!data || !length)return MTP_PROTOCOL;
    if(length>64u*1024u*1024u)return MTP_LIMIT;
    image_layout layout;
    if(image_header(data,length,&layout) && !image_complete(data,length,layout.png))return MTP_PROTOCOL;
    psmtp_decode_budget *previous=psmtp_decode_current,*budget=calloc(1,sizeof(*budget));
    if(!budget)return MTP_MEMORY;
    budget->refs=1;budget->limit=160u*1024u*1024u-length;
    psmtp_decode_current=budget;
    if(stbi_info_from_memory(data,(int)length,&w,&h,&channels) && w>0 && h>0){
        size_t count=(size_t)w*h;
        if(count>24u*1024u*1024u)rc=MTP_LIMIT;
        else {
            unsigned char *decoded=stbi_load_from_memory(data,(int)length,&w,&h,&channels,4);
            if(decoded){
                int orient=orientation(data,length);
                *width=orient>=5?(unsigned)h:(unsigned)w;
                *height=orient>=5?(unsigned)w:(unsigned)h;
                *pixels=malloc(count*4);
                rc=*pixels?0:MTP_MEMORY;
                if(*pixels)for(unsigned y=0;y<*height;++y)for(unsigned x=0;x<*width;++x){
                    unsigned sx=x,sy=y;
                    if(orient==2)sx=w-1-x;else if(orient==3){sx=w-1-x;sy=h-1-y;}
                    else if(orient==4)sy=h-1-y;else if(orient==5){sx=y;sy=x;}
                    else if(orient==6){sx=y;sy=h-1-x;}else if(orient==7){sx=w-1-y;sy=h-1-x;}
                    else if(orient==8){sx=w-1-y;sy=x;}
                    memcpy(*pixels+(size_t)y*(*width)+x,decoded+((size_t)sy*w+sx)*4,4);
                }
            }
        }
    }
    psmtp_decode_cleanup(budget);
    psmtp_decode_current=previous;
    if(budget->limited)rc=MTP_LIMIT;else if(budget->oom)rc=MTP_MEMORY;
    psmtp_decode_release(budget);
    if(rc){free(*pixels);*pixels=NULL;*width=*height=0;}
    return rc;
}
int psmtp_metadata_image(const uint8_t *bytes,size_t length,psmtp_metadata *m){
    int w=0,h=0,channels=0,rc=MTP_PROTOCOL;unsigned orient,rw,rh,tw,th;unsigned char *rgba=NULL;
    image_layout layout;psmtp_decode_budget *budget=NULL,*previous=psmtp_decode_current;uint64_t estimate=0,reserved;
    free(m->image);m->image=NULL;m->image_width=m->image_height=0;m->fallback_image=NULL;m->preview_reason=PSMTP_PREVIEW_NONE;
    m->decode_peak_bytes=m->decode_live_bytes=m->decode_estimate_bytes=0;m->decode_allocations=m->decode_started=0;
    m->decode_encoded_bytes=length;
    if(!bytes || !length){psmtp_metadata_image_failed(m,PSMTP_PREVIEW_INVALID_IMAGE,MTP_PROTOCOL);return MTP_PROTOCOL;}
    reserved=(uint64_t)length+sizeof(psmtp_decode_budget)+16+PREVIEW_THUMBNAIL_MAX_BYTES;
    if(length>PREVIEW_ENCODED_MAX_BYTES || reserved>=PREVIEW_DECODE_BUDGET_BYTES){psmtp_metadata_image_failed(m,PSMTP_PREVIEW_ENCODED_LIMIT,MTP_LIMIT);return MTP_LIMIT;}
    if(!image_header(bytes,length,&layout)){psmtp_metadata_image_failed(m,PSMTP_PREVIEW_INVALID_IMAGE,MTP_PROTOCOL);return MTP_PROTOCOL;}
    m->width=layout.w;m->height=layout.h;
    if(layout.w>STBI_MAX_DIMENSIONS || layout.h>STBI_MAX_DIMENSIONS){psmtp_metadata_image_failed(m,PSMTP_PREVIEW_DIMENSIONS,MTP_LIMIT);return MTP_LIMIT;}
    if(!image_complete(bytes,length,layout.png)){psmtp_metadata_image_failed(m,PSMTP_PREVIEW_INVALID_IMAGE,MTP_PROTOCOL);return MTP_PROTOCOL;}
    budget=calloc(1,sizeof(*budget));if(!budget){psmtp_metadata_image_failed(m,PSMTP_PREVIEW_SYSTEM_MEMORY,MTP_MEMORY);return MTP_MEMORY;}
    budget->refs=1;budget->limit=(size_t)(PREVIEW_DECODE_BUDGET_BYTES-reserved);psmtp_decode_current=budget;
    if(!stbi_info_from_memory(bytes,(int)length,&w,&h,&channels) || w<=0 || h<=0 ||
       (unsigned)w!=layout.w || (unsigned)h!=layout.h){m->preview_reason=PSMTP_PREVIEW_INVALID_IMAGE;goto done;}
    if(!image_estimate(&layout,length,&estimate) || !image_add(&estimate,reserved) || estimate>PREVIEW_DECODE_BUDGET_BYTES){
        m->decode_estimate_bytes=estimate>SIZE_MAX?SIZE_MAX:(size_t)estimate;m->preview_reason=PSMTP_PREVIEW_ESTIMATE_LIMIT;rc=MTP_LIMIT;goto done;}
    m->decode_estimate_bytes=(size_t)estimate;
    orient=orientation(bytes,length);rw=orient>=5?(unsigned)h:(unsigned)w;rh=orient>=5?(unsigned)w:(unsigned)h;m->width=rw;m->height=rh;
    m->decode_started=1;rgba=stbi_load_from_memory(bytes,(int)length,&w,&h,&channels,4);
    if(!rgba){m->preview_reason=PSMTP_PREVIEW_INVALID_IMAGE;goto done;}
    tw=rw>256?256:rw;th=(unsigned)((uint64_t)rh*tw/rw);if(th>256){th=256;tw=(unsigned)((uint64_t)rw*th/rh);}if(!tw)tw=1;if(!th)th=1;
    m->image=malloc((size_t)tw*th*4);if(!m->image){m->preview_reason=PSMTP_PREVIEW_SYSTEM_MEMORY;rc=MTP_MEMORY;goto done;}
    m->image_width=tw;m->image_height=th;
    /* Walk the decoded pixels in display orientation without allocating a
     * rotated copy or changing the preview's decode budget. */
    const unsigned char *origin=rgba;
    ptrdiff_t step_right=4,step_down=(ptrdiff_t)w*4;
    if(orient==2 || orient==3 || orient==7 || orient==8){origin+=(w-1)*step_right;step_right=-step_right;}
    if(orient==3 || orient==4 || orient==6 || orient==7){origin+=(h-1)*step_down;step_down=-step_down;}
    ptrdiff_t step_x=orient>=5?step_down:step_right,step_y=orient>=5?step_right:step_down;
    psmtp_image_taps columns[256];
    for(unsigned x=0;x<tw;++x)columns[x]=psmtp_image_axis(x,rw,tw);
    for(unsigned y=0;y<th;++y){
        psmtp_image_taps ty=psmtp_image_axis(y,rh,th);
        for(unsigned x=0;x<tw;++x)m->image[y*tw+x]=psmtp_image_sample(origin,step_x,step_y,columns[x],ty);
    }
    m->preview_reason=PSMTP_PREVIEW_OK;rc=0;
done:
    stbi_image_free(rgba);
    /* The pinned decoder has conversion-failure paths retaining the source
     * allocation. Drain the operation's registry even on such early returns. */
    psmtp_decode_cleanup(budget);
    m->decode_live_bytes=__atomic_load_n(&budget->live,__ATOMIC_ACQUIRE);
    m->decode_peak_bytes=(size_t)reserved+budget->peak;m->decode_allocations=budget->allocations;
    if(budget->limited){m->preview_reason=PSMTP_PREVIEW_ALLOCATION_LIMIT;rc=MTP_LIMIT;}
    else if(budget->oom){m->preview_reason=PSMTP_PREVIEW_SYSTEM_MEMORY;rc=MTP_MEMORY;}
    psmtp_decode_current=previous;psmtp_decode_release(budget);
    if(rc)psmtp_metadata_image_failed(m,m->preview_reason,rc);else {m->loaded=1;m->error=0;}
    return rc;
}
static int read_at(psmtp_meta_reader *r,uint64_t offset,void *out,size_t n){if(offset>r->size || n>r->size-offset || r->consumed>16u*1024u*1024u || n>16u*1024u*1024u-r->consumed)return MTP_LIMIT;r->consumed+=n;return r->read(r->user,offset,out,n);}
static uint8_t *block(psmtp_meta_reader *r,uint64_t offset,size_t n){uint8_t *p;if(!n || n>12u*1024u*1024u)return NULL;p=malloc(n);if(!p)return NULL;if(read_at(r,offset,p,n)){free(p);return NULL;}return p;}
int psmtp_metadata_image_reader(psmtp_meta_reader *r,uint64_t offset,uint64_t length,psmtp_metadata *m){
    uint8_t *p;int rc;
    m->decode_peak_bytes=m->decode_live_bytes=m->decode_estimate_bytes=0;m->decode_allocations=m->decode_started=0;
    m->decode_encoded_bytes=length>SIZE_MAX?SIZE_MAX:(size_t)length;
    if(!length || length>PREVIEW_ENCODED_MAX_BYTES || length>=PREVIEW_DECODE_BUDGET_BYTES-PREVIEW_THUMBNAIL_MAX_BYTES-sizeof(psmtp_decode_budget)-16){
        psmtp_metadata_image_failed(m,PSMTP_PREVIEW_ENCODED_LIMIT,MTP_LIMIT);return MTP_LIMIT;}
    p=malloc((size_t)length);if(!p){psmtp_metadata_image_failed(m,PSMTP_PREVIEW_SYSTEM_MEMORY,MTP_MEMORY);return MTP_MEMORY;}
    m->decode_peak_bytes=(size_t)length+16;
    rc=read_at(r,offset,p,(size_t)length);
    if(!rc)rc=psmtp_metadata_image(p,(size_t)length,m);else psmtp_metadata_image_failed(m,PSMTP_PREVIEW_SOURCE_READ,rc);
    free(p);return rc;
}
static void sfo(psmtp_metadata *m,const uint8_t *p,size_t n){
    uint32_t keys,values,count,i;if(n<20 || memcmp(p,"\0PSF",4))return;keys=le32(p+8);values=le32(p+12);count=le32(p+16);if(count>1024 || 20u+(uint64_t)count*16>n || keys>=n || values>=n)return;
    for(i=0;i<count;++i){const uint8_t *e=p+20+i*16;size_t key=(size_t)keys+ex16(e,1),offset=(size_t)values+le32(e+12),len=le32(e+4);char *dst=NULL;size_t cap=0;if(key>=n || !memchr(p+key,0,n-key) || offset>=n || !len || len>n-offset || !memchr(p+offset,0,len))continue;
        if(!strcmp((const char *)p+key,"TITLE")){dst=m->title;cap=sizeof(m->title);}else if(!strcmp((const char *)p+key,"CONTENT_ID")){dst=m->content_id;cap=sizeof(m->content_id);}else if(!strcmp((const char *)p+key,"TITLE_ID")){dst=m->title_id;cap=sizeof(m->title_id);}else if(!strcmp((const char *)p+key,"APP_VER")){dst=m->version;cap=sizeof(m->version);}if(dst)snprintf(dst,cap,"%s",p+offset);
    }
}
static int pkg(psmtp_meta_reader *r,psmtp_metadata *m){
    uint8_t header[32];uint32_t table,count,i;int rc=read_at(r,0,header,sizeof(header));if(rc)return rc;if(memcmp(header,"\177CNT",4))return MTP_PROTOCOL;count=be32(header+16);table=be32(header+24);if(count>8192 || table>r->size || (uint64_t)count*32>r->size-table)return MTP_PROTOCOL;
    for(i=0;i<count;++i){uint8_t entry[32],*data;uint32_t id,offset,length;rc=read_at(r,(uint64_t)table+i*32,entry,32);if(rc)return rc;id=be32(entry);if(id!=0x1000 && id!=0x1200)continue;offset=be32(entry+16);length=be32(entry+20);
        if(id==0x1200){if(length>4u*1024u*1024u){m->decode_encoded_bytes=length;psmtp_metadata_image_failed(m,PSMTP_PREVIEW_ENCODED_LIMIT,MTP_LIMIT);return MTP_LIMIT;}rc=psmtp_metadata_image_reader(r,offset,length,m);if(rc)return rc;}
        else {if(length>131072u)continue;data=block(r,offset,length);if(data){sfo(m,data,length);free(data);}}}
    return 0;
}
static int mp4_atom_header(psmtp_meta_reader *r,uint64_t at,uint64_t end,uint8_t h[16],uint64_t *size,uint64_t *head){
    if(end-at<8)return MTP_LIMIT;
    int rc=read_at(r,at,h,8);
    if(rc)return rc;
    *size=be32(h);*head=8;
    if(*size==1){
        if(end-at<16 || read_at(r,at+8,h+8,8))return MTP_PROTOCOL;
        *size=be64(h+8);*head=16;
    }
    if(!*size)*size=end-at;
    return *size<*head || *size>end-at?MTP_PROTOCOL:0;
}

/* Scan only the track hierarchy and inspect the first sample description of
 * an audio track. Skip media data and unrelated file metadata. */
static int mp4_audio_codec(psmtp_meta_reader *r,uint64_t start,uint64_t end,unsigned depth,unsigned *visited,uint32_t *codec){
    static const char *const containers[]={"moov","trak","mdia","minf","stbl"};
    int is_audio=0;
    uint32_t description=0;
    for(uint64_t at=start;at<end;){
        uint8_t h[16];uint64_t size,head;
        if(++*visited>4096)return MTP_LIMIT;
        int rc=mp4_atom_header(r,at,end,h,&size,&head);
        if(rc)return rc;
        if(depth==3 && !memcmp(h+4,"hdlr",4) && size-head>=12){
            rc=read_at(r,at+head,h,12);
            if(rc)return rc;
            is_audio=!memcmp(h+8,"soun",4);
        }else if(depth==5 && !memcmp(h+4,"stsd",4) && size-head>=16){
            rc=read_at(r,at+head,h,16);
            if(rc)return rc;
            uint32_t entry_size=be32(h+8);
            if(be32(h+4) && entry_size>=8 && entry_size<=size-head-8){
                *codec=be32(h+12);
                return 0;
            }
        }else if(depth<5 && !memcmp(h+4,containers[depth],4)){
            rc=mp4_audio_codec(r,at+head,at+size,depth+1,visited,&description);
            if(rc && rc!=MTP_UNSUPPORTED)return rc;
            if(!rc && depth!=3){*codec=description;return 0;}
        }
        at+=size;
    }
    if(is_audio && description){*codec=description;return 0;}
    return MTP_UNSUPPORTED;
}

int psmtp_metadata_mp4_audio_codec(psmtp_meta_reader *r,uint32_t *codec){
    unsigned visited=0;
    *codec=0;r->consumed=0;
    return mp4_audio_codec(r,0,r->size,0,&visited,codec);
}

static int atoms(psmtp_meta_reader *r,psmtp_metadata *m,uint64_t start,uint64_t end,unsigned depth,unsigned *visited,int cover){
    uint64_t at=start;if(depth>9)return MTP_LIMIT;
    while(at<end){uint8_t h[40];uint64_t size,head,data;int rc;if(++*visited>4096)return MTP_LIMIT;rc=mp4_atom_header(r,at,end,h,&size,&head);if(rc)return rc;data=at+head;
        if(!memcmp(h+4,"mvhd",4) && size-head>=24){size_t length=size-head<40?(size_t)(size-head):40;if(!read_at(r,data,h,length)){uint32_t scale=h[0]==1?be32(h+20):be32(h+12);uint64_t duration=h[0]==1?length>=32?be64(h+24):0:be32(h+16);if(scale && duration<=UINT64_MAX/1000)m->duration_ms=duration*1000/scale;}}
        else if(!memcmp(h+4,"tkhd",4) && size-head>=8){if(!read_at(r,at+size-8,h,8)){unsigned w=be32(h)>>16,height=be32(h+4)>>16;if(w && height){m->width=w;m->height=height;}}}
        else if(!memcmp(h+4,"stsd",4) && size-head>=16){if(!read_at(r,data,h,16)){memcpy(m->codec,h+12,4);m->codec[4]=0;}}
        else if(!memcmp(h+4,"data",4) && cover && size-head>8){unsigned w=m->width,height=m->height;
            if(size-head>4u*1024u*1024u){m->decode_encoded_bytes=(size_t)(size-head-8);psmtp_metadata_image_failed(m,PSMTP_PREVIEW_ENCODED_LIMIT,MTP_LIMIT);return MTP_LIMIT;}
            rc=psmtp_metadata_image_reader(r,data+8,size-head-8,m);if(w && height){m->width=w;m->height=height;}if(rc)return rc;}
        else if(!memcmp(h+4,"moov",4)||!memcmp(h+4,"trak",4)||!memcmp(h+4,"mdia",4)||!memcmp(h+4,"minf",4)||!memcmp(h+4,"stbl",4)||!memcmp(h+4,"udta",4)||!memcmp(h+4,"ilst",4)||!memcmp(h+4,"covr",4)||!memcmp(h+4,"meta",4)){
            uint64_t skip=!memcmp(h+4,"meta",4)?4:0;if(size-head>=skip){rc=atoms(r,m,data+skip,at+size,depth+1,visited,cover || !memcmp(h+4,"covr",4));if(rc && rc!=MTP_UNSUPPORTED)return rc;}}
        at+=size;
    }return 0;
}
int psmtp_metadata_load(psmtp_meta_reader *r,const char *name,psmtp_metadata *m){
    const char *ext=strrchr(name,'.');int rc=MTP_UNSUPPORTED;memset(m,0,sizeof(*m));r->consumed=0;
    if(ext && !psmtp_ui_name_compare(ext,".pkg"))rc=pkg(r,m);
    else if(ext && (!psmtp_ui_name_compare(ext,".jpg") || !psmtp_ui_name_compare(ext,".jpeg") || !psmtp_ui_name_compare(ext,".png")))rc=psmtp_metadata_image_reader(r,0,r->size,m);
    else if(ext && (!psmtp_ui_name_compare(ext,".mp4") || !psmtp_ui_name_compare(ext,".mov") || !psmtp_ui_name_compare(ext,".m4v"))){unsigned visited=0;rc=atoms(r,m,0,r->size,0,&visited,0);}
    m->loaded=1;m->error=rc;return rc;
}
