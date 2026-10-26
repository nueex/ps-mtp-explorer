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
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_MAX_DIMENSIONS 8192
#include <stb_image.h>
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
void psmtp_metadata_free(psmtp_metadata *m){free(m->image);memset(m,0,sizeof(*m));}
int psmtp_metadata_image(const uint8_t *bytes,size_t length,psmtp_metadata *m){
    int w,h,channels;unsigned orient,x,y,rw,rh,tw,th;unsigned char *rgba;
    if(!length || length>12u*1024u*1024u || !stbi_info_from_memory(bytes,(int)length,&w,&h,&channels) || w<=0 || h<=0)return MTP_UNSUPPORTED;
    orient=orientation(bytes,length);rw=orient>=5?(unsigned)h:(unsigned)w;rh=orient>=5?(unsigned)w:(unsigned)h;m->width=rw;m->height=rh;
    if((uint64_t)w*h>24000000u)return MTP_LIMIT;
    rgba=stbi_load_from_memory(bytes,(int)length,&w,&h,&channels,4);if(!rgba)return MTP_PROTOCOL;
    tw=rw>256?256:rw;th=(unsigned)((uint64_t)rh*tw/rw);if(th>256){th=256;tw=(unsigned)((uint64_t)rw*th/rh);}if(!tw)tw=1;if(!th)th=1;
    free(m->image);m->image=malloc((size_t)tw*th*4);if(!m->image){stbi_image_free(rgba);return MTP_MEMORY;}
    m->image_width=tw;m->image_height=th;
    for(y=0;y<th;++y)for(x=0;x<tw;++x){unsigned rx=(unsigned)((uint64_t)x*rw/tw),ry=(unsigned)((uint64_t)y*rh/th),sx=rx,sy=ry;const uint8_t *p;
        if(orient==2)sx=w-1-rx;else if(orient==3){sx=w-1-rx;sy=h-1-ry;}else if(orient==4)sy=h-1-ry;
        else if(orient==5){sx=ry;sy=rx;}else if(orient==6){sx=ry;sy=h-1-rx;}else if(orient==7){sx=w-1-ry;sy=h-1-rx;}else if(orient==8){sx=w-1-ry;sy=rx;}
        p=rgba+((size_t)sy*w+sx)*4;m->image[y*tw+x]=p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
    }
    stbi_image_free(rgba);return 0;
}
static int read_at(psmtp_meta_reader *r,uint64_t offset,void *out,size_t n){if(offset>r->size || n>r->size-offset || n>16u*1024u*1024u-r->consumed)return MTP_LIMIT;r->consumed+=n;return r->read(r->user,offset,out,n);}
static uint8_t *block(psmtp_meta_reader *r,uint64_t offset,size_t n){uint8_t *p;if(!n || n>12u*1024u*1024u)return NULL;p=malloc(n);if(!p)return NULL;if(read_at(r,offset,p,n)){free(p);return NULL;}return p;}
static void sfo(psmtp_metadata *m,const uint8_t *p,size_t n){
    uint32_t keys,values,count,i;if(n<20 || memcmp(p,"\0PSF",4))return;keys=le32(p+8);values=le32(p+12);count=le32(p+16);if(count>1024 || 20u+(uint64_t)count*16>n || keys>=n || values>=n)return;
    for(i=0;i<count;++i){const uint8_t *e=p+20+i*16;size_t key=(size_t)keys+ex16(e,1),offset=(size_t)values+le32(e+12),len=le32(e+4);char *dst=NULL;size_t cap=0;if(key>=n || !memchr(p+key,0,n-key) || offset>=n || !len || len>n-offset || !memchr(p+offset,0,len))continue;
        if(!strcmp((const char *)p+key,"TITLE")){dst=m->title;cap=sizeof(m->title);}else if(!strcmp((const char *)p+key,"CONTENT_ID")){dst=m->content_id;cap=sizeof(m->content_id);}else if(!strcmp((const char *)p+key,"TITLE_ID")){dst=m->title_id;cap=sizeof(m->title_id);}else if(!strcmp((const char *)p+key,"APP_VER")){dst=m->version;cap=sizeof(m->version);}if(dst)snprintf(dst,cap,"%s",p+offset);
    }
}
static int pkg(psmtp_meta_reader *r,psmtp_metadata *m){
    uint8_t header[32];uint32_t table,count,i;int rc=read_at(r,0,header,sizeof(header));if(rc)return rc;if(memcmp(header,"\177CNT",4))return MTP_PROTOCOL;count=be32(header+16);table=be32(header+24);if(count>8192 || table>r->size || (uint64_t)count*32>r->size-table)return MTP_PROTOCOL;
    for(i=0;i<count;++i){uint8_t entry[32],*data;uint32_t id,offset,length;rc=read_at(r,(uint64_t)table+i*32,entry,32);if(rc)return rc;id=be32(entry);if(id!=0x1000 && id!=0x1200)continue;offset=be32(entry+16);length=be32(entry+20);if(length>(id==0x1000?131072u:4u*1024u*1024u))continue;data=block(r,offset,length);if(!data)continue;if(id==0x1000)sfo(m,data,length);else (void)psmtp_metadata_image(data,length,m);free(data);}
    return 0;
}
static int atoms(psmtp_meta_reader *r,psmtp_metadata *m,uint64_t start,uint64_t end,unsigned depth,unsigned *visited){
    uint64_t at=start;if(depth>9)return MTP_LIMIT;
    while(at<end){uint8_t h[40];uint64_t size,head=8,data;int rc;if(++*visited>4096 || end-at<8)return MTP_LIMIT;rc=read_at(r,at,h,8);if(rc)return rc;size=be32(h);if(size==1){if(end-at<16 || read_at(r,at+8,h+8,8))return MTP_PROTOCOL;size=be64(h+8);head=16;}if(!size)size=end-at;if(size<head || size>end-at)return MTP_PROTOCOL;data=at+head;
        if(!memcmp(h+4,"mvhd",4) && size-head>=24){size_t length=size-head<40?(size_t)(size-head):40;if(!read_at(r,data,h,length)){uint32_t scale=h[0]==1?be32(h+20):be32(h+12);uint64_t duration=h[0]==1?length>=32?be64(h+24):0:be32(h+16);if(scale && duration<=UINT64_MAX/1000)m->duration_ms=duration*1000/scale;}}
        else if(!memcmp(h+4,"tkhd",4) && size-head>=8){if(!read_at(r,at+size-8,h,8)){unsigned w=be32(h)>>16,height=be32(h+4)>>16;if(w && height){m->width=w;m->height=height;}}}
        else if(!memcmp(h+4,"stsd",4) && size-head>=16){if(!read_at(r,data,h,16)){memcpy(m->codec,h+12,4);m->codec[4]=0;}}
        else if(!memcmp(h+4,"data",4) && depth && size-head>8 && size-head<=4u*1024u*1024u){uint8_t *p=block(r,data+8,(size_t)(size-head-8));if(p){unsigned w=m->width,height=m->height;(void)psmtp_metadata_image(p,(size_t)(size-head-8),m);if(w && height){m->width=w;m->height=height;}free(p);}}
        else if(!memcmp(h+4,"moov",4)||!memcmp(h+4,"trak",4)||!memcmp(h+4,"mdia",4)||!memcmp(h+4,"minf",4)||!memcmp(h+4,"stbl",4)||!memcmp(h+4,"udta",4)||!memcmp(h+4,"ilst",4)||!memcmp(h+4,"covr",4)||!memcmp(h+4,"meta",4)){
            uint64_t skip=!memcmp(h+4,"meta",4)?4:0;if(size-head>=skip){rc=atoms(r,m,data+skip,at+size,depth+1,visited);if(rc && rc!=MTP_UNSUPPORTED)return rc;}}
        at+=size;
    }return 0;
}
int psmtp_metadata_load(psmtp_meta_reader *r,const char *name,psmtp_metadata *m){
    const char *ext=strrchr(name,'.');int rc=MTP_UNSUPPORTED;memset(m,0,sizeof(*m));r->consumed=0;
    if(ext && !psmtp_ui_name_compare(ext,".pkg"))rc=pkg(r,m);
    else if(ext && (!psmtp_ui_name_compare(ext,".jpg") || !psmtp_ui_name_compare(ext,".jpeg") || !psmtp_ui_name_compare(ext,".png"))){uint8_t *data=block(r,0,(size_t)(r->size>SIZE_MAX?SIZE_MAX:r->size));if(data){rc=psmtp_metadata_image(data,(size_t)r->size,m);free(data);}else rc=MTP_LIMIT;}
    else if(ext && (!psmtp_ui_name_compare(ext,".mp4") || !psmtp_ui_name_compare(ext,".mov") || !psmtp_ui_name_compare(ext,".m4v"))){unsigned visited=0;rc=atoms(r,m,0,r->size,0,&visited);}
    m->loaded=1;m->error=rc;return rc;
}
