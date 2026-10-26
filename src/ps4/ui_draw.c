/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#include "ui_draw.h"
#include "ui_font.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "reference_assets.inc"
#define W 1920
#define H 1080
static uint32_t *pixels,*background;
typedef struct {int kind,a,b,c,d,w,h;uint32_t fill,stroke,*data;uint64_t used;} surface;
static surface surfaces[64];static uint64_t surface_clock;static size_t surface_bytes;
static surface *cached(int kind,int a,int b,int c,int d,uint32_t fill,uint32_t stroke,int w,int h) {
    unsigned i,slot=0;size_t bytes=(size_t)w*h*4;
    for(i=0;i<64;++i) {surface *s=&surfaces[i];if(s->used && s->kind==kind && s->a==a && s->b==b && s->c==c && s->d==d && s->fill==fill && s->stroke==stroke) {s->used=++surface_clock;return s;}if(s->used<surfaces[slot].used)slot=i;}
    if(bytes>4u*1024u*1024u) return NULL;
    while(surface_bytes+bytes>16u*1024u*1024u) {unsigned oldest=64;for(i=0;i<64;++i)if(surfaces[i].data && (oldest==64 || surfaces[i].used<surfaces[oldest].used))oldest=i;if(oldest==64)break;surface_bytes-=(size_t)surfaces[oldest].w*surfaces[oldest].h*4;free(surfaces[oldest].data);memset(&surfaces[oldest],0,sizeof(surface));slot=oldest;}
    {surface *s=&surfaces[slot];if(s->data){surface_bytes-=(size_t)s->w*s->h*4;free(s->data);}memset(s,0,sizeof(*s));s->data=calloc((size_t)w*h,4);if(!s->data)return NULL;s->kind=kind;s->a=a;s->b=b;s->c=c;s->d=d;s->w=w;s->h=h;s->fill=fill;s->stroke=stroke;surface_bytes+=bytes;return s;}
}
static uint32_t mix(uint32_t dst,uint32_t src,unsigned a) {
    unsigned inv=255-a;
    return 0xff000000u|(((dst&255)*inv+(src&255)*a+127)/255)|
      (((((dst>>8)&255)*inv+((src>>8)&255)*a+127)/255)<<8)|
      (((((dst>>16)&255)*inv+((src>>16)&255)*a+127)/255)<<16);
}
static void put(int x,int y,uint32_t color,unsigned alpha) {if(x>=0 && x<W && y>=0 && y<H && alpha) pixels[y*W+x]=alpha>=255?color:mix(pixels[y*W+x],color,alpha);}
static void blit(int x,int y,const uint32_t *data,int w,int h) {int row,col;for(row=0;row<h;++row){if(y+row<0 || y+row>=H)continue;for(col=0;col<w;++col){uint32_t c=data[row*w+col];unsigned a=c>>24;if(x+col<0 || x+col>=W || !a)continue;if(a==255)pixels[(y+row)*W+x+col]=c;else pixels[(y+row)*W+x+col]=mix(pixels[(y+row)*W+x+col],c,a);}}}
void ui_rect(int x,int y,int w,int h,uint32_t c) {
    int row,col,x0=x<0?0:x,y0=y<0?0:y,x1=x+w>W?W:x+w,y1=y+h>H?H:y+h;
    for(row=y0;row<y1;++row) for(col=x0;col<x1;++col) pixels[row*W+col]=c;
}
void ui_alpha(int x,int y,int w,int h,uint32_t c,unsigned a) {
    int row,col,x0=x<0?0:x,y0=y<0?0:y,x1=x+w>W?W:x+w,y1=y+h>H?H:y+h;
    for(row=y0;row<y1;++row) for(col=x0;col<x1;++col) put(col,row,c,a);
}
static float box_distance(float x,float y,int w,int h,int r) {
    float dx=fabsf(x-w*0.5f)-(w*0.5f-r),dy=fabsf(y-h*0.5f)-(h*0.5f-r);
    float ax=dx>0?dx:0,ay=dy>0?dy:0,inside=dx>dy?dx:dy;
    return sqrtf(ax*ax+ay*ay)+(inside<0?inside:0)-r;
}
void ui_box(int x,int y,int w,int h,int r,uint32_t fill,uint32_t stroke,int border) {
    surface *s=cached(1,w,h,r,border,fill,stroke,w,h);
    if(s && s->used){blit(x,y,s->data,w,h);return;}
    int i,j;for(j=0;j<h;++j) for(i=0;i<w;++i) {
        float d=box_distance(i+0.5f,j+0.5f,w,h,r);unsigned a=d<=-0.5f?255:d>=0.5f?0:(unsigned)((0.5f-d)*255);
        if(a) {uint32_t color=border && d>-(float)border?stroke:fill;unsigned alpha=a*(color>>24)/255;if(s)s->data[j*w+i]=(color&0xffffffu)|(alpha<<24);else put(x+i,y+j,color,alpha);}
    }
    if(s){s->used=++surface_clock;blit(x,y,s->data,w,h);}
}
void ui_line(int x0,int y0,int x1,int y1,int width,uint32_t c,unsigned a) {
    int x,y,minx=(x0<x1?x0:x1)-width,maxx=(x0>x1?x0:x1)+width,miny=(y0<y1?y0:y1)-width,maxy=(y0>y1?y0:y1)+width;
    float vx=(float)(x1-x0),vy=(float)(y1-y0),length=vx*vx+vy*vy;
    for(y=miny;y<=maxy;++y) for(x=minx;x<=maxx;++x) {
        float t=length?((x-x0)*vx+(y-y0)*vy)/length:0,dx,dy,coverage;if(t<0)t=0;if(t>1)t=1;
        dx=x-(x0+vx*t);dy=y-(y0+vy*t);coverage=width*0.5f+0.5f-sqrtf(dx*dx+dy*dy);
        if(coverage>0) put(x,y,c,coverage>=1?a:(unsigned)(a*coverage));
    }
}
void ui_ring(int cx,int cy,int r,int width,uint32_t c,unsigned a) {
    int x,y;for(y=-r-2;y<=r+2;++y) for(x=-r-2;x<=r+2;++x) {
        float coverage=width*0.5f+0.5f-fabsf(sqrtf((float)(x*x+y*y))-r);
        if(coverage>0) put(cx+x,cy+y,c,coverage>=1?a:(unsigned)(a*coverage));
    }
}
void ui_shadow(int x,int y,int w,int h,int r) {
    int xx,yy,i;surface *s=cached(2,w,h,r,0,0,0,w+24,h+24);
    if(s && s->used){blit(x-12,y-9,s->data,w+24,h+24);return;}
    for(yy=-12;yy<h+12;++yy) for(xx=-12;xx<w+12;++xx) {
        unsigned alpha=0;if(xx>=0 && xx<w && yy>=0 && yy<h)continue;
        for(i=12;i>0;--i)if(xx>=-i && xx<w+i && yy>=-i && yy<h+i && box_distance(xx+i+0.5f,yy+i+0.5f,w+2*i,h+2*i,r+i)<0)alpha=255-((255-alpha)*249+127)/255;
        if(s)s->data[(yy+12)*(w+24)+xx+12]=alpha<<24;else put(x+xx,y+yy+3,UI_RGB(0,0,0),alpha);
    }
    if(s){s->used=++surface_clock;blit(x-12,y-9,s->data,w+24,h+24);}
}
void ui_art(int id,int x,int y,int w,int h,uint32_t tint) {
    static const psmtp_ui_asset *const assets[]={&ui_wordmark,&ui_platform,&ui_folder,&ui_phone,&ui_usb,&ui_drive,&ui_settings,&ui_home,&ui_copy,&ui_move,&ui_rename,&ui_delete,&ui_info,&ui_document,&ui_thumb_pkg,&ui_thumb_image,&ui_thumb_video};
    const psmtp_ui_asset *im;int xx,yy,fit;if(id<0 || id>UI_THUMB_VIDEO || w<=0 || h<=0) return;im=assets[id];

    if(id==UI_WORDMARK || id>=UI_THUMB_PKG) {
        for(yy=0;yy<h;++yy) for(xx=0;xx<w;++xx) {
            const unsigned char *p=im->rgba+((yy*im->h/h)*im->w+xx*im->w/w)*4;
            put(x+xx,y+yy,tint?tint:UI_RGB(p[0],p[1],p[2]),p[3]);
        }
        return;
    }

    if(w*im->h>h*im->w) {fit=h*im->w/im->h;x+=(w-fit)/2;w=fit;}
    else {fit=w*im->h/im->w;y+=(h-fit)/2;h=fit;}
    if(w<=0 || h<=0) return;
    {surface *s=cached(3,id,w,h,0,tint,0,w,h);
    if(s && s->used){blit(x,y,s->data,w,h);return;}
    for(yy=0;yy<h;++yy) for(xx=0;xx<w;++xx) {
        int sx=((2*xx+1)*im->w*128/w)-128,sy=((2*yy+1)*im->h*128/h)-128;
        unsigned x0,y0,x1,y1,fx,fy,weights[4],alpha=0,k;uint64_t red=0,green=0,blue=0;
        const unsigned char *samples[4];uint32_t c;
        if(sx<0)sx=0;if(sy<0)sy=0;x0=(unsigned)sx/256;y0=(unsigned)sy/256;
        x1=x0+1<(unsigned)im->w?x0+1:x0;y1=y0+1<(unsigned)im->h?y0+1:y0;
        fx=(unsigned)sx%256;fy=(unsigned)sy%256;
        weights[0]=(256-fx)*(256-fy);weights[1]=fx*(256-fy);weights[2]=(256-fx)*fy;weights[3]=fx*fy;
        samples[0]=im->rgba+(y0*im->w+x0)*4;samples[1]=im->rgba+(y0*im->w+x1)*4;
        samples[2]=im->rgba+(y1*im->w+x0)*4;samples[3]=im->rgba+(y1*im->w+x1)*4;
        for(k=0;k<4;++k) {unsigned a=samples[k][3]*weights[k];alpha+=a;if(!tint) {red+=(uint64_t)samples[k][0]*a;green+=(uint64_t)samples[k][1]*a;blue+=(uint64_t)samples[k][2]*a;}}
        if(!alpha)continue;c=tint?tint:UI_RGB((red+alpha/2)/alpha,(green+alpha/2)/alpha,(blue+alpha/2)/alpha);
        if(s)s->data[yy*w+xx]=(c&0xffffffu)|(((alpha+32768)/65536)<<24);else put(x+xx,y+yy,c,(alpha+32768)/65536);
    }
    if(s){s->used=++surface_clock;blit(x,y,s->data,w,h);}}
}
void ui_text(int x,int y,int px,int width,const char *s,uint32_t c) {psmtp_font_draw(pixels,x,y,px,width,s?s:"",c);}
void ui_center(int x,int y,int px,int width,const char *s,uint32_t c) {int text=psmtp_font_width(s,px);ui_text(x+(text<width?(width-text)/2:0),y,px,width,s,c);}
void ui_backdrop(void) {
    int x,y;uint32_t *save=pixels;
    if(!background) {
        unsigned seed=31;background=malloc(W*H*sizeof(*background));if(!background) {ui_rect(0,0,W,H,UI_RGB(18,16,17));return;}pixels=background;
        for(y=0;y<H;++y) for(x=0;x<W;++x) {
            float center=1.0f-fabsf((x-W*0.5f)/(W*0.5f));int shade=0,r,g,b;
            seed=seed*1664525u+1013904223u;
            if(y<122) shade=11+(int)(center*7);
            else if(y>=950) shade=1+(int)(center*2);
            else if(x<364 || x>=1478) shade=1+(int)(center*2);
            shade+=(int)((seed>>29)&1);r=18+shade;g=16+shade;b=17+shade;
            pixels[y*W+x]=UI_RGB(r,g,b);
        }
        for(y=0;y<13;++y) for(x=0;x<20;++x) {
            int cx=12+x*103+(y%2)*30,cy=145+y*62+(x%3)*10,kind=(x+3*y)%4;uint32_t c=UI_RGB(141,127,120);
            if(cy<270 || (cx>364 && cx<1478 && cy<920)) continue;
            if(kind==0) ui_ring(cx,cy,6,1,c,40);
            else if(kind==1) {ui_line(cx-5,cy-5,cx+5,cy+5,1,c,44);ui_line(cx+5,cy-5,cx-5,cy+5,1,c,44);}
            else if(kind==2) {ui_line(cx,cy-6,cx-6,cy+5,1,c,40);ui_line(cx-6,cy+5,cx+6,cy+5,1,c,40);ui_line(cx+6,cy+5,cx,cy-6,1,c,40);}
            else {ui_line(cx-5,cy-5,cx+5,cy-5,1,c,36);ui_line(cx+5,cy-5,cx+5,cy+5,1,c,36);ui_line(cx+5,cy+5,cx-5,cy+5,1,c,36);ui_line(cx-5,cy+5,cx-5,cy-5,1,c,36);}
        }
        ui_rect(0,121,W,1,UI_RGB(53,48,49));ui_rect(363,122,1,828,UI_RGB(39,42,49));ui_rect(1478,122,1,828,UI_RGB(39,42,49));ui_rect(0,950,W,1,UI_RGB(55,48,49));
        pixels=save;
    }
    memcpy(pixels,background,W*H*sizeof(*pixels));
}
void ui_button(int kind,int x,int y,int size,uint32_t color){
    int c=x+size/2,cy=y+size/2,r=size/3,w=size>30?3:2;
    if(kind==UI_BTN_CROSS){ui_line(c-r,cy-r,c+r,cy+r,w,color,255);ui_line(c+r,cy-r,c-r,cy+r,w,color,255);}
    else if(kind==UI_BTN_CIRCLE)ui_ring(c,cy,r,w,color,255);
    else if(kind==UI_BTN_TRIANGLE){ui_line(c,y+size/6,x+size/6,y+size*5/6,w,color,255);ui_line(x+size/6,y+size*5/6,x+size*5/6,y+size*5/6,w,color,255);ui_line(x+size*5/6,y+size*5/6,c,y+size/6,w,color,255);}
    else if(kind==UI_BTN_SQUARE){ui_box(x+size/6,y+size/6,size*2/3,size*2/3,1,0,color,w);}
    else if(kind==UI_BTN_DPAD){ui_box(x+size/3,y,size/3,size,2,color,0,0);ui_box(x,y+size/3,size,size/3,2,color,0,0);}
    else if(kind==UI_BTN_OPTIONS){int i;ui_box(x+size/5,y,size*3/5,size,5,0,color,2);for(i=0;i<3;++i)ui_rect(x+size/3,y+size/3+i*size/6,size/3,2,color);}
    else if(kind==UI_BTN_TOUCH){ui_box(x,y+size/5,size,size*3/5,4,0,color,2);ui_rect(x+size/3,cy-2,2,2,color);ui_rect(x+size*2/3,cy-2,2,2,color);}
    else {static const char *const labels[]={"L1","R1","L2","R2","L3","R3"};int k=kind-UI_BTN_L1;if(k<0 || k>5)return;
        if(k>=4){ui_ring(c,cy,size/2-1,2,color,255);ui_ring(c,cy-size/9,size/3,1,color,255);}else ui_box(x,y+size/6,size,size*2/3,5,0,color,2);
        ui_center(x,y+size/5,size/2,size,labels[k],color);
    }
}
void ui_image(const uint32_t *data,unsigned width,unsigned height,int x,int y,int w,int h) {
    int row,col,tw,th;if(!data || !width || !height || w<=0 || h<=0)return;
    tw=w;th=(int)((uint64_t)height*w/width);if(th>h){th=h;tw=(int)((uint64_t)width*h/height);}if(!tw || !th)return;x+=(w-tw)/2;y+=(h-th)/2;
    for(row=0;row<th;++row)for(col=0;col<tw;++col){uint32_t c=data[(size_t)((uint64_t)row*height/th)*width+(unsigned)((uint64_t)col*width/tw)];put(x+col,y+row,c,c>>24);}
}
void ui_draw_begin(uint32_t *p) {pixels=p;}
void ui_draw_close(void) {unsigned i;free(background);background=NULL;pixels=NULL;for(i=0;i<64;++i)free(surfaces[i].data);memset(surfaces,0,sizeof(surfaces));surface_bytes=0;surface_clock=0;}
