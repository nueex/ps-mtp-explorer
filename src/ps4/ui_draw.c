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
#include "preferences.h"
#include "../image_scale.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "reference_assets.inc"
#define W 1920
#define H 1080
#include "theme_colors.inc"
static uint32_t *pixels,*background;
static uint32_t *settings_background;
static int theme=PSMTP_THEME_CLASSIC;

void ui_draw_theme(int choice) {
    psmtp_font_theme(choice!=PSMTP_THEME_CLASSIC);
    if(choice==theme)return;
    theme=choice;
    free(background);background=NULL;
    free(settings_background);settings_background=NULL;
}

uint32_t ui_theme_color(uint32_t c) {
    if(theme==PSMTP_THEME_CLASSIC || !c)return c;
    int r=c&255,g=(c>>8)&255,b=(c>>16)&255;
    /* Keep the controller, artwork, warning and black overlay colors. */
    if(r-g>17 || g-r>17 || r-b>17 || b-r>17 || r<15)return c;
    if(r<95){
        int shade=theme==PSMTP_THEME_LIGHT?r+22:r*3/4;
        return UI_RGB(shade,shade+2,shade+15);
    }
    if(r<190)return theme==PSMTP_THEME_LIGHT?UI_RGB(r+19,r+19,r+29):UI_RGB(r-18,r-17,r-7);
    if(c==UI_WHITE)return theme==PSMTP_THEME_LIGHT?UI_RGB(248,245,250):UI_RGB(236,234,241);
    return UI_RGB(r,g<250?g+5:255,b<245?b+10:255);
}
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
static const unsigned char gradient_dither[8][8]={
    {0,32,8,40,2,34,10,42},{48,16,56,24,50,18,58,26},
    {12,44,4,36,14,46,6,38},{60,28,52,20,62,30,54,22},
    {3,35,11,43,1,33,9,41},{51,19,59,27,49,17,57,25},
    {15,47,7,39,13,45,5,37},{63,31,55,23,61,29,53,21}
};
static uint32_t gradient_color(const uint32_t *stops,unsigned cols,unsigned rows,unsigned x,unsigned y,unsigned w,unsigned h){
    unsigned sx=w>1?x*(cols-1)*255/(w-1):0,sy=h>1?y*(rows-1)*255/(h-1):0;
    unsigned ix=sx/255,iy=sy/255,fx=sx%255,fy=sy%255;
    unsigned right=ix+(ix+1<cols),bottom=iy+(iy+1<rows);
    const uint32_t colors[]={stops[iy*cols+ix],stops[iy*cols+right],stops[bottom*cols+ix],stops[bottom*cols+right]};
    const unsigned weights[]={(255-fx)*(255-fy),fx*(255-fy),(255-fx)*fy,fx*fy};
    unsigned threshold=(2*gradient_dither[y&7][x&7]+1)*65025u/128;
    uint32_t result=0xff000000u;
    for(unsigned shift=0;shift<24;shift+=8){
        unsigned sum=threshold;
        for(unsigned corner=0;corner<4;++corner)sum+=((colors[corner]>>shift)&255)*weights[corner];
        result|=(sum/65025u)<<shift;
    }
    return result;
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
static float box_radius(int w,int h,int r) {
    float limit=(w<h?w:h)*0.5f;
    return r<0?0:r>limit?limit:(float)r;
}
static float box_distance(float x,float y,int w,int h,float r) {
    float dx=fabsf(x-w*0.5f)-(w*0.5f-r),dy=fabsf(y-h*0.5f)-(h*0.5f-r);
    return (dx>0 && dy>0?sqrtf(dx*dx+dy*dy):fmaxf(dx,dy))-r;
}

enum { COVERAGE_SAMPLES=4, COVERAGE_FULL=16 };
static float sample_offset(int i) {return (i+0.5f)/COVERAGE_SAMPLES-0.5f;}

static unsigned box_coverage(int x,int y,int w,int h,float r) {
    if(x<0 || y<0 || x>=w || y>=h)return 0;
    if((x>=r && w-1-x>=r) || (y>=r && h-1-y>=r))return COVERAGE_FULL;
    float dx=r-fminf(x+0.5f,w-x-0.5f);
    float dy=r-fminf(y+0.5f,h-y-0.5f);
    float ax=fmaxf(0,dx),ay=fmaxf(0,dy),distance=ax*ax+ay*ay;
    if(r>=0.707107f && distance<=(r-0.707107f)*(r-0.707107f))return COVERAGE_FULL;
    if(distance>=(r+0.707107f)*(r+0.707107f))return 0;
    unsigned covered=0;
    for(int j=0;j<COVERAGE_SAMPLES;++j)for(int i=0;i<COVERAGE_SAMPLES;++i){
        float sx=fmaxf(0,dx+sample_offset(i)),sy=fmaxf(0,dy+sample_offset(j));
        covered+=sx*sx+sy*sy<=r*r;
    }
    return covered;
}

/* Resolve the fill and the non-overlapping stroke together before blending
 * with the frame. A translucent fill must not show through the stroke. */
static uint32_t box_pixel(uint32_t fill,unsigned inner,uint32_t stroke,unsigned outer) {
    unsigned f=inner*(fill>>24),s=(outer-inner)*(stroke>>24),total=f+s;
    unsigned alpha=(total+COVERAGE_FULL/2)/COVERAGE_FULL;
    if(!alpha)return 0;
    uint32_t color=fill;
    if(!f)color=stroke;
    else if(s)color=mix(fill,stroke,(s*255+total/2)/total);
    return (color&0xffffffu)|(alpha<<24);
}
static void draw_box(int x,int y,int w,int h,int r,uint32_t fill,uint32_t stroke,int border,int kind,const uint32_t *gradient) {
    if(w<=0 || h<=0)return;
    surface *s=cached(kind,w,h,r,border,fill,stroke,w,h);
    if(s && s->used){blit(x,y,s->data,w,h);return;}
    float radius=box_radius(w,h,r),inside_radius=fmaxf(0,radius-border);
    for(int j=0;j<h;++j)for(int i=0;i<w;++i){
        unsigned outer=box_coverage(i,j,w,h,radius);
        if(!outer)continue;
        unsigned inner=border?box_coverage(i-border,j-border,w-2*border,h-2*border,inside_radius):outer;
        if(inner>outer)inner=outer;
        uint32_t color=inner && gradient?gradient_color(gradient,5,4,(unsigned)i,(unsigned)j,(unsigned)w,(unsigned)h):fill;
        uint32_t pixel=box_pixel(color,inner,stroke,outer);
        if(s)s->data[j*w+i]=pixel;
        else put(x+i,y+j,pixel,pixel>>24);
    }
    if(s){s->used=++surface_clock;blit(x,y,s->data,w,h);}
}
void ui_box(int x,int y,int w,int h,int r,uint32_t fill,uint32_t stroke,int border){
    draw_box(x,y,w,h,r,fill,stroke,border,1,NULL);
}
void ui_explorer_card(unsigned card,int x,int y,int w,int h,int radius,int selected){
    const uint32_t *colors=theme==PSMTP_THEME_DARK?dark_cards[card]:light_cards[card];
    uint32_t border=selected?UI_RGB(246,244,250):theme==PSMTP_THEME_DARK?UI_RGB(54,55,67):UI_RGB(91,93,109);
    draw_box(x,y,w,h,radius,0,border,selected?4:2,4+(int)card+13*theme,colors);
}
static float segment_distance_squared(float x,float y,float vx,float vy,float inverse_length) {
    float t=fminf(1,fmaxf(0,(x*vx+y*vy)*inverse_length));
    float dx=x-vx*t,dy=y-vy*t;
    return dx*dx+dy*dy;
}
void ui_line(int x0,int y0,int x1,int y1,int width,uint32_t c,unsigned a) {
    int x,y,minx=(x0<x1?x0:x1)-width,maxx=(x0>x1?x0:x1)+width,miny=(y0<y1?y0:y1)-width,maxy=(y0>y1?y0:y1)+width;
    float vx=(float)(x1-x0),vy=(float)(y1-y0),length=vx*vx+vy*vy;
    float inverse_length=length?1/length:0,radius=width*0.5f;
    float inside=fmaxf(0,radius-0.707107f),outside=radius+0.707107f;
    for(y=miny;y<=maxy;++y) for(x=minx;x<=maxx;++x) {
        float dx=(float)(x-x0),dy=(float)(y-y0),distance=segment_distance_squared(dx,dy,vx,vy,inverse_length);
        if(distance>=outside*outside)continue;
        unsigned covered=COVERAGE_FULL;
        if(distance>=inside*inside){
            covered=0;
            for(int j=0;j<COVERAGE_SAMPLES;++j)for(int i=0;i<COVERAGE_SAMPLES;++i)
                covered+=segment_distance_squared(dx+sample_offset(i),dy+sample_offset(j),vx,vy,inverse_length)<=radius*radius;
        }
        put(x,y,c,(a*covered+COVERAGE_FULL/2)/COVERAGE_FULL);
    }
}
void ui_ring(int cx,int cy,int r,int width,uint32_t c,unsigned a) {
    float inner=fmaxf(0,r-width*0.5f),outer=r+width*0.5f;
    int extent=(int)ceilf(outer+0.707107f);
    for(int y=-extent;y<=extent;++y)for(int x=-extent;x<=extent;++x){
        float distance=sqrtf((float)(x*x+y*y));
        if(distance>=outer+0.707107f || distance<inner-0.707107f)continue;
        unsigned covered=COVERAGE_FULL;
        if(distance>outer-0.707107f || (inner && distance<inner+0.707107f)){
            covered=0;
            for(int j=0;j<COVERAGE_SAMPLES;++j)for(int i=0;i<COVERAGE_SAMPLES;++i){
                float sx=x+sample_offset(i),sy=y+sample_offset(j),d=sx*sx+sy*sy;
                covered+=d>=inner*inner && d<=outer*outer;
            }
        }
        put(cx+x,cy+y,c,(a*covered+COVERAGE_FULL/2)/COVERAGE_FULL);
    }
}
typedef struct {int x,y;} ui_point;

/* Vertices are clockwise in screen coordinates. */
static void polygon(const ui_point *points,int n,uint32_t color) {
    int left=points[0].x,right=left,top=points[0].y,bottom=top;
    int dx[6],dy[6];
    for(int i=0;i<n;++i){
        ui_point p=points[i],next=points[(i+1)%n];
        if(p.x<left)left=p.x;
        if(p.x>right)right=p.x;
        if(p.y<top)top=p.y;
        if(p.y>bottom)bottom=p.y;
        dx[i]=next.x-p.x;
        dy[i]=next.y-p.y;
    }
    /* Each sample lives on an integer grid at 1/8 pixel increments. */
    for(int y=top;y<bottom;++y)for(int x=left;x<right;++x){
        unsigned covered=0;
        for(int j=0;j<COVERAGE_SAMPLES;++j)for(int i=0;i<COVERAGE_SAMPLES;++i){
            int edge;
            for(edge=0;edge<n;++edge){
                int sx=8*(x-points[edge].x)+2*i+1,sy=8*(y-points[edge].y)+2*j+1;
                if(dx[edge]*sy<dy[edge]*sx)break;
            }
            covered+=edge==n;
        }
        put(x,y,color,((color>>24)*covered+COVERAGE_FULL/2)/COVERAGE_FULL);
    }
}
void ui_arrow(int x,int y,int right,uint32_t color) {
    int base=x+(right?0:17),tip=x+(right?17:0);
    ui_point points[]={{base,y+17*(right?0:2)},{tip,y+17},{base,y+17*(right?2:0)}};
    polygon(points,3,color);
}
void ui_shadow(int x,int y,int w,int h,int r) {
    if(w<=0 || h<=0)return;
    surface *s=cached(2,w,h,r,0,0,0,w+24,h+24);
    if(s && s->used){blit(x-12,y-9,s->data,w+24,h+24);return;}
    float radius=box_radius(w,h,r);
    for(int yy=-12;yy<h+12;++yy)for(int xx=-12;xx<w+12;++xx){
        unsigned panel=box_coverage(xx,yy+3,w,h,radius);
        if(panel==COVERAGE_FULL)continue;
        float distance=box_distance(xx+0.5f,yy+0.5f,w,h,radius);
        float falloff=fminf(1,fmaxf(0,1-distance/12));
        unsigned alpha=(unsigned)(64*falloff*falloff*(COVERAGE_FULL-panel)/COVERAGE_FULL+0.5f);
        if(s)s->data[(yy+12)*(w+24)+xx+12]=alpha<<24;else put(x+xx,y+yy+3,UI_RGB(0,0,0),alpha);
    }
    if(s){s->used=++surface_clock;blit(x-12,y-9,s->data,w+24,h+24);}
}
void ui_art(int id,int x,int y,int w,int h,uint32_t tint) {
    static const psmtp_ui_asset *const assets[]={&ui_wordmark,&ui_platform,&ui_folder,&ui_phone,&ui_usb,&ui_drive,&ui_settings,&ui_home,&ui_copy,&ui_move,&ui_rename,&ui_delete,&ui_info,&ui_document,&ui_thumb_pkg,&ui_thumb_image,&ui_thumb_video};
    const psmtp_ui_asset *im;int xx,yy,fit;if(id<0 || id>UI_THUMB_VIDEO || w<=0 || h<=0) return;im=assets[id];

    if(id!=UI_WORDMARK && id<UI_THUMB_PKG){
        if(w*im->h>h*im->w) {fit=h*im->w/im->h;x+=(w-fit)/2;w=fit;}
        else {fit=w*im->h/im->w;y+=(h-fit)/2;h=fit;}
    }
    if(w<=0 || h<=0) return;
    surface *s=cached(3,id,w,h,0,tint,0,w,h);
    if(s && s->used){blit(x,y,s->data,w,h);return;}
    for(yy=0;yy<h;++yy){
        psmtp_image_taps ty=psmtp_image_axis((unsigned)yy,(unsigned)im->h,(unsigned)h);
        for(xx=0;xx<w;++xx){
            psmtp_image_taps tx=psmtp_image_axis((unsigned)xx,(unsigned)im->w,(unsigned)w);
            uint32_t pixel=psmtp_image_sample(im->rgba,4,im->w*4,tx,ty);
            if(tint)pixel=(pixel&0xff000000u)|(tint&0xffffffu);
            if(s)s->data[yy*w+xx]=pixel;
            else put(x+xx,y+yy,pixel,pixel>>24);
        }
    }
    if(s){s->used=++surface_clock;blit(x,y,s->data,w,h);}
}
static int music_note_contains(float x,float y){
    /* Use the same proportions at every size. The stem joins a filled,
     * slanted notehead, and the flag curves to a tapered end. */
    float dx=x-0.31f,dy=y-0.75f;
    float major=(0.94f*dx-0.34f*dy)/0.23f;
    float minor=(0.34f*dx+0.94f*dy)/0.155f;
    if(major*major+minor*minor<=1.0f)return 1;
    const float stem_right=0.5325f; /* Tangent to the right edge of the notehead. */
    if(x>=0.412f && x<=stem_right && y>=0.075f && y<=0.709f)return 1;
    if(x<stem_right || x>0.91f)return 0;
    float t=(x-stem_right)/(0.91f-stem_right);
    float top=0.075f+t*(0.14f+0.27f*t);
    float bottom=0.23f+t*(0.18f+0.105f*t);
    return y>=top && y<=bottom;
}
void ui_music_note(int x,int y,int size,uint32_t color){
    enum { MUSIC_NOTE_SURFACE=64 };
    if(size<=0)return;
    surface *s=cached(MUSIC_NOTE_SURFACE,size,0,0,0,color,0,size,size);
    if(s && s->used){blit(x,y,s->data,size,size);return;}
    float scale=1.0f/size;
    for(int row=0;row<size;++row)for(int col=0;col<size;++col){
        unsigned covered=0;
        for(int j=0;j<COVERAGE_SAMPLES;++j)for(int i=0;i<COVERAGE_SAMPLES;++i)
            covered+=music_note_contains((col+0.5f+sample_offset(i))*scale,(row+0.5f+sample_offset(j))*scale);
        unsigned alpha=((color>>24)*covered+COVERAGE_FULL/2)/COVERAGE_FULL;
        if(s)s->data[row*size+col]=(color&0xffffffu)|(alpha<<24);
        else put(x+col,y+row,color,alpha);
    }
    if(s){s->used=++surface_clock;blit(x,y,s->data,size,size);}
}
void ui_file_icon(psmtp_file_kind kind,int x,int y,int size) {
    static const uint32_t colors[PSMTP_KIND_COUNT]={
        [PSMTP_KIND_IMAGE]=UI_RGB(24,119,137), [PSMTP_KIND_VIDEO]=UI_RGB(133,73,166),
        [PSMTP_KIND_AUDIO]=UI_RGB(158,59,112), [PSMTP_KIND_PACKAGE]=UI_RGB(163,96,35),
        [PSMTP_KIND_ARCHIVE]=UI_RGB(144,73,37), [PSMTP_KIND_SHEET]=UI_RGB(31,121,79),
        [PSMTP_KIND_PDF]=UI_RGB(174,63,69), [PSMTP_KIND_DOCUMENT]=UI_RGB(49,99,170),
        [PSMTP_KIND_SLIDES]=UI_RGB(172,80,46), [PSMTP_KIND_DISK]=UI_RGB(67,102,131),
        [PSMTP_KIND_INSTALLER]=UI_RGB(85,113,49)
    };
    if(kind==PSMTP_KIND_FOLDER){
        ui_art(UI_FOLDER,x,y,size,size,0);
        return;
    }
    if(kind<PSMTP_KIND_IMAGE || kind>=PSMTP_KIND_COUNT){
        ui_art(UI_DOCUMENT,x,y,size,size,0);
        return;
    }
    const uint32_t ink=UI_RGB(252,248,247),paper=colors[kind];
    int p[33];
    for(int i=0;i<=32;++i)p[i]=(i*size+16)/32;
#define ICON_X(i) (x+p[i])
#define ICON_Y(i) (y+p[i])
#define ICON_LINE(a,b,c,d) ui_line(ICON_X(a),ICON_Y(b),ICON_X(c),ICON_Y(d),stroke,ink,255)
    int stroke=p[2]>0?p[2]:1;
    ui_box(ICON_X(4),ICON_Y(1),p[28]-p[4],p[31]-p[1],p[3],paper,mix(paper,ink,38),1);
    const ui_point fold[]={{ICON_X(20),ICON_Y(2)},{ICON_X(27),ICON_Y(9)},{ICON_X(20),ICON_Y(9)}};
    polygon(fold,3,mix(paper,ink,75));
    switch(kind){
    case PSMTP_KIND_IMAGE:
        ui_ring(ICON_X(20),ICON_Y(13),p[1],stroke,ink,255);
        ICON_LINE(9,23,14,16); ICON_LINE(14,16,19,22); ICON_LINE(19,22,21,19);
        ICON_LINE(21,19,24,23); ICON_LINE(24,23,9,23);
        break;
    case PSMTP_KIND_VIDEO: {
        const ui_point play[]={{ICON_X(12),ICON_Y(11)},{ICON_X(23),ICON_Y(18)},{ICON_X(12),ICON_Y(25)}};
        polygon(play,3,ink);
        break;
    }
    case PSMTP_KIND_AUDIO:
        ui_music_note(ICON_X(7),ICON_Y(9),p[25]-p[7],ink);
        break;
    case PSMTP_KIND_PACKAGE:
        ICON_LINE(9,15,16,11); ICON_LINE(16,11,23,15); ICON_LINE(23,15,23,22);
        ICON_LINE(23,22,16,26); ICON_LINE(16,26,9,22); ICON_LINE(9,22,9,15);
        ICON_LINE(9,15,16,19); ICON_LINE(16,19,23,15); ICON_LINE(16,19,16,26);
        break;
    case PSMTP_KIND_ARCHIVE:
        for(int i=0;i<4;++i){
            int left=i%2?16:12;
            ui_rect(ICON_X(left),ICON_Y(7+3*i),p[left+4]-p[left],stroke,ink);
        }
        ui_box(ICON_X(13),ICON_Y(20),p[20]-p[13],p[27]-p[20],p[1],0,ink,stroke);
        break;
    case PSMTP_KIND_SHEET:
        ui_box(ICON_X(8),ICON_Y(11),p[24]-p[8],p[25]-p[11],p[1],0,ink,stroke);
        ICON_LINE(9,16,23,16); ICON_LINE(9,20,23,20); ICON_LINE(15,12,15,24);
        break;
    case PSMTP_KIND_PDF:
        ICON_LINE(9,23,16,12); ICON_LINE(16,12,22,23);
        ICON_LINE(22,23,11,21); ICON_LINE(11,21,23,18);
        break;
    case PSMTP_KIND_DOCUMENT:
        ICON_LINE(10,13,19,13); ICON_LINE(10,18,22,18); ICON_LINE(10,23,22,23);
        break;
    case PSMTP_KIND_SLIDES:
        ui_box(ICON_X(8),ICON_Y(11),p[24]-p[8],p[23]-p[11],p[1],0,ink,stroke);
        ICON_LINE(11,27,21,27); ICON_LINE(16,23,16,26); ICON_LINE(12,19,20,15);
        break;
    case PSMTP_KIND_DISK:
        ui_ring(ICON_X(16),ICON_Y(18),p[7],stroke,ink,255);
        ui_ring(ICON_X(16),ICON_Y(18),p[1],stroke,ink,255);
        break;
    case PSMTP_KIND_INSTALLER:
        ICON_LINE(16,11,16,21); ICON_LINE(12,17,16,21); ICON_LINE(20,17,16,21);
        ICON_LINE(10,25,22,25);
        break;
    default:
        break;
    }
#undef ICON_LINE
#undef ICON_Y
#undef ICON_X
}
void ui_text(int x,int y,int px,int width,const char *s,uint32_t c) {psmtp_font_draw(pixels,x,y,px,width,s?s:"",c);}
void ui_center(int x,int y,int px,int width,const char *s,uint32_t c) {int text=psmtp_font_width(s,px);ui_text(x+(text<width?(width-text)/2:0),y,px,width,s,c);}
static float backdrop_sample(float previous,float left,float right,float next,float t){
    float difference=right-left;
    float incoming=(right-previous)*0.5f,outgoing=(next-left)*0.5f;
    float value=left+t*(incoming+t*(3*difference-2*incoming-outgoing+
        t*(incoming+outgoing-2*difference)));
    return fminf(fmaxf(left,right),fmaxf(fminf(left,right),value));
}
static void paint_gradient(const uint32_t *stops,unsigned cols,unsigned rows,int x,int y,unsigned w,unsigned h){
    size_t stride=(size_t)w*3;
    float *horizontal=malloc(rows*stride*sizeof(*horizontal));
    if(!horizontal){
        for(unsigned j=0;j<h;++j)for(unsigned i=0;i<w;++i)
            pixels[(y+j)*W+x+i]=gradient_color(stops,cols,rows,i,j,w,h);
        return;
    }
    /* Keep the original color stops, with continuous slopes between them.
     * Quantize only once with sub-level dithering for the 8-bit output. */
    for(unsigned i=0;i<w;++i){
        float position=(float)(i*(cols-1))/(w-1);
        unsigned a=(unsigned)position,b=a+(a+1<cols);
        unsigned previous=a-(a>0),next=b+(b+1<cols);
        for(unsigned row=0;row<rows;++row){
            const uint32_t *colors=stops+row*cols;
            float *pixel=horizontal+row*stride+3*i;
            for(unsigned c=0;c<3;++c){
                unsigned shift=8*c;
                pixel[c]=backdrop_sample((colors[previous]>>shift)&255,(colors[a]>>shift)&255,
                    (colors[b]>>shift)&255,(colors[next]>>shift)&255,position-a);
            }
        }
    }
    for(unsigned j=0;j<h;++j){
        float position=(float)(j*(rows-1))/(h-1);
        unsigned a=(unsigned)position,b=a+(a+1<rows);
        const float *previous=horizontal+(a-(a>0))*stride,*top=horizontal+a*stride;
        const float *bottom=horizontal+b*stride,*next=horizontal+(b+(b+1<rows))*stride;
        uint32_t *dest=pixels+(y+j)*W+x;
        const unsigned char *thresholds=gradient_dither[(y+j)&7];
        for(unsigned i=0;i<w;++i){
            float threshold=(thresholds[(x+i)&7]+0.5f)/64.0f;
            uint32_t color=0xff000000u;
            for(unsigned c=0;c<3;++c){
                unsigned k=3*i+c;
                float channel=backdrop_sample(previous[k],top[k],bottom[k],next[k],position-a);
                color|=(uint32_t)(channel+threshold)<<(c*8);
            }
            dest[i]=color;
        }
    }
    free(horizontal);
}
static void reference_backdrop(int settings) {
    int dark=theme==PSMTP_THEME_DARK;
    uint32_t edge=dark?UI_RGB(35,39,51):UI_RGB(73,75,90);
    paint_gradient(dark?dark_header:light_header,17,4,0,0,W,122);
    paint_gradient(dark?dark_footer:light_footer,17,5,0,951,W,129);
    if(settings){
        paint_gradient(dark?dark_files:light_files,19,16,0,122,W,828);
    }else {
        paint_gradient(dark?dark_sidebar:light_sidebar,8,15,0,122,363,828);
        paint_gradient(dark?dark_files:light_files,19,16,364,122,1114,828);
        paint_gradient(dark?dark_inspector:light_inspector,10,16,1479,122,441,828);
        ui_rect(363,122,1,828,edge);ui_rect(1478,122,1,828,edge);
    }
    ui_rect(0,122,W,1,edge);ui_rect(0,950,W,1,edge);
}
static void draw_backdrop(int settings) {
    int x,y;uint32_t *save=pixels;uint32_t **cache=settings?&settings_background:&background;uint32_t *base=*cache;
    if(!base) {
        unsigned seed=31;base=malloc(W*H*sizeof(*base));*cache=base;if(!base) {ui_rect(0,0,W,H,UI_RGB(18,16,17));return;}pixels=base;
        if(theme!=PSMTP_THEME_CLASSIC)reference_backdrop(settings);
        else {
        for(y=0;y<H;++y) for(x=0;x<W;++x) {
            float center=1.0f-fabsf((x-W*0.5f)/(W*0.5f));int shade=0,r,g,b;
            seed=seed*1664525u+1013904223u;
            if(y<122) shade=11+(int)(center*7);
            else if(y>=950) shade=1+(int)(center*2);
            else if(settings || x<364 || x>=1478) shade=1+(int)(center*2);
            shade+=(int)((seed>>29)&1);r=18+shade;g=16+shade;b=17+shade;
            pixels[y*W+x]=UI_RGB(r,g,b);
        }
        for(y=0;y<13;++y) for(x=0;x<20;++x) {
            int cx=12+x*103+(y%2)*30,cy=145+y*62+(x%3)*10,kind=(x+3*y)%4;uint32_t c=UI_RGB(141,127,120);
            if(cy<270 || (!settings && cx>364 && cx<1478 && cy<920)) continue;
            if(kind==0) ui_ring(cx,cy,6,1,c,40);
            else if(kind==1) {ui_line(cx-5,cy-5,cx+5,cy+5,1,c,44);ui_line(cx+5,cy-5,cx-5,cy+5,1,c,44);}
            else if(kind==2) {ui_line(cx,cy-6,cx-6,cy+5,1,c,40);ui_line(cx-6,cy+5,cx+6,cy+5,1,c,40);ui_line(cx+6,cy+5,cx,cy-6,1,c,40);}
            else {ui_line(cx-5,cy-5,cx+5,cy-5,1,c,36);ui_line(cx+5,cy-5,cx+5,cy+5,1,c,36);ui_line(cx+5,cy+5,cx-5,cy+5,1,c,36);ui_line(cx-5,cy+5,cx-5,cy-5,1,c,36);}
        }
        ui_rect(0,121,W,1,UI_RGB(53,48,49));if(!settings){ui_rect(363,122,1,828,UI_RGB(39,42,49));ui_rect(1478,122,1,828,UI_RGB(39,42,49));}ui_rect(0,950,W,1,UI_RGB(55,48,49));
        }
        pixels=save;
    }
    memcpy(pixels,base,W*H*sizeof(*pixels));
}
void ui_backdrop(void){draw_backdrop(0);}
void ui_settings_backdrop(void){draw_backdrop(1);}
void ui_button(int kind,int x,int y,int size,uint32_t color){
    int c=x+size/2,cy=y+size/2,r=size/3,w=size>30?3:2;
    if(kind==UI_BTN_CROSS){ui_line(c-r,cy-r,c+r,cy+r,w,color,255);ui_line(c+r,cy-r,c-r,cy+r,w,color,255);}
    else if(kind==UI_BTN_CIRCLE)ui_ring(c,cy,r,w,color,255);
    else if(kind==UI_BTN_TRIANGLE){ui_line(c,y+size/6,x+size/6,y+size*5/6,w,color,255);ui_line(x+size/6,y+size*5/6,x+size*5/6,y+size*5/6,w,color,255);ui_line(x+size*5/6,y+size*5/6,c,y+size/6,w,color,255);}
    else if(kind==UI_BTN_SQUARE){
        int half=size*5/18;
        ui_box(c-half,cy-half,2*half+1,2*half+1,0,0,color,w);
    }
    else if(kind==UI_BTN_DPAD){
        int g=size>28?3:2,b=g,o=size>28?13:size*11/28;
        const ui_point up[]={{-b,-o},{b+1,-o},{b+1,-g-b},{0,-g},{-b,-g-b}};
        for(int arm=0;arm<4;++arm){
            ui_point points[5];
            for(int i=0;i<5;++i){
                int dx=up[i].x,dy=up[i].y;
                if(arm&1){int swap=dx;dx=-dy;dy=swap;}
                if(arm&2){dx=-dx;dy=-dy;}
                points[i]=(ui_point){c+dx,cy+dy};
            }
            polygon(points,5,color);
        }
    }
    else if(kind==UI_BTN_OPTIONS){int i;ui_box(x+size/5,y,size*3/5,size,5,0,color,2);for(i=0;i<3;++i)ui_rect(x+size/3,y+size/3+i*size/6,size/3,2,color);}
    else if(kind==UI_BTN_TOUCH){ui_box(x,y+size/5,size,size*3/5,4,0,color,2);ui_rect(x+size/3,cy-2,2,2,color);ui_rect(x+size*2/3,cy-2,2,2,color);}
    else {static const char *const labels[]={"L1","R1","L2","R2","L3","R3"};int k=kind-UI_BTN_L1;if(k<0 || k>5)return;
        if(k>=4){ui_ring(c,cy,size/2-1,2,color,255);ui_ring(c,cy-size/9,size/3,1,color,255);}else ui_box(x,y+size/6,size,size*2/3,5,0,color,2);
        ui_center(x,y+size/5,size/2,size,labels[k],color);
    }
}
static struct {
    const uint32_t *source;
    unsigned stride,width,height;
    int w,h;
    uint32_t *data;
    size_t capacity;
} photo_cache;

void ui_image_reset(void){
    free(photo_cache.data);
    memset(&photo_cache,0,sizeof(photo_cache));
}
static void draw_image(const uint32_t *data,unsigned stride,unsigned width,unsigned height,
                       int x,int y,int w,int h,int cache) {
    if(!data || !width || !height || w<=0 || h<=0 || w>W || h>H)return;
    int tw=w,th=h;
    if((uint64_t)height*w<=(uint64_t)width*h)th=(int)((uint64_t)height*w/width);
    else tw=(int)((uint64_t)width*h/height);
    if(!tw || !th)return;
    x+=(w-tw)/2;y+=(h-th)/2;
    if(tw==(int)width && th==(int)height){
        for(int row=0;row<th;++row)blit(x,y+row,data+(size_t)row*stride,tw,1);
        return;
    }
    uint32_t *scaled=NULL;
    if(cache){
        if(photo_cache.source==data && photo_cache.stride==stride && photo_cache.width==width &&
           photo_cache.height==height && photo_cache.w==tw && photo_cache.h==th){
            blit(x,y,photo_cache.data,tw,th);
            return;
        }
        size_t count=(size_t)tw*th;
        if(count>photo_cache.capacity){
            uint32_t *next=realloc(photo_cache.data,count*sizeof(*next));
            if(next){photo_cache.data=next;photo_cache.capacity=count;}
        }
        if(photo_cache.capacity>=count){
            photo_cache.source=data;photo_cache.stride=stride;photo_cache.width=width;photo_cache.height=height;
            photo_cache.w=tw;photo_cache.h=th;scaled=photo_cache.data;
        }
    }
    psmtp_image_taps columns[W];
    for(int col=0;col<tw;++col)columns[col]=psmtp_image_axis((unsigned)col,width,(unsigned)tw);
    for(int row=0;row<th;++row){
        psmtp_image_taps ty=psmtp_image_axis((unsigned)row,height,(unsigned)th);
        for(int col=0;col<tw;++col){
            uint32_t pixel=psmtp_image_sample((const unsigned char *)data,4,(ptrdiff_t)stride*4,columns[col],ty);
            if(scaled)scaled[(size_t)row*tw+col]=pixel;
            else put(x+col,y+row,pixel,pixel>>24);
        }
    }
    if(scaled)blit(x,y,scaled,tw,th);
}
void ui_image_region(const uint32_t *data,unsigned stride,unsigned width,unsigned height,unsigned sx,unsigned sy,int x,int y,int w,int h) {
    if(!data || sx>=stride || width>stride-sx)return;
    draw_image(data+(size_t)sy*stride+sx,stride,width,height,x,y,w,h,1);
}
void ui_image(const uint32_t *data,unsigned width,unsigned height,int x,int y,int w,int h){
    draw_image(data,width,width,height,x,y,w,h,0);
}

/* Opaque video can interpolate two packed channels at a time. */
static uint32_t video_mix(uint32_t a,uint32_t b,unsigned weight){
    unsigned left=256-weight;
    uint32_t rb=(((a&0xff00ff)*left+(b&0xff00ff)*weight+0x800080)>>8)&0xff00ff;
    uint32_t g=(((a&0xff00)*left+(b&0xff00)*weight+0x8000)>>8)&0xff00;
    return 0xff000000u|rb|g;
}
static void video_row(uint32_t *out,const uint32_t *src,const psmtp_image_taps *columns,unsigned width){
    for(unsigned i=0;i<width;++i){
        psmtp_image_taps x=columns[i];
        out[i]=video_mix(src[x.first],src[x.last],x.last_weight);
    }
}
void ui_video(const uint32_t *data,unsigned width,unsigned height,float aspect){
    if(!data || !width || !height)return;
    if(!(aspect>=0.25f && aspect<=8.0f))aspect=(float)width/height;
    unsigned w=W,h=(unsigned)(W/aspect+0.5f);
    if(h>H){h=H;w=(unsigned)(H*aspect+0.5f);}
    if(!w || !h)return;
    unsigned x=(W-w)/2,y=(H-h)/2;
    uint32_t black=UI_RGB(0,0,0);
    ui_rect(0,0,W,(int)y,black);
    ui_rect(0,(int)(y+h),W,(int)(H-y-h),black);
    ui_rect(0,(int)y,(int)x,(int)h,black);
    ui_rect((int)(x+w),(int)y,(int)(W-x-w),(int)h,black);
    psmtp_image_taps columns[W];
    uint32_t rows[2][W];
    unsigned loaded[2]={UINT32_MAX,UINT32_MAX};
    if(w!=width)for(unsigned i=0;i<w;++i)columns[i]=psmtp_image_linear_axis(i,width,w);
    for(unsigned row=0;row<h;++row){
        psmtp_image_taps ty=psmtp_image_linear_axis(row,height,h);
        const uint32_t *first=data+(size_t)ty.first*width,*last=data+(size_t)ty.last*width;
        uint32_t *dst=pixels+(size_t)(y+row)*W+x;
        if(w!=width){
            unsigned a=ty.first%2,b=ty.last%2;
            if(loaded[a]!=ty.first){video_row(rows[a],first,columns,w);loaded[a]=ty.first;}
            if(loaded[b]!=ty.last){video_row(rows[b],last,columns,w);loaded[b]=ty.last;}
            first=rows[a];last=rows[b];
        }
        if(!ty.last_weight)memcpy(dst,first,w*sizeof(*dst));
        else for(unsigned i=0;i<w;++i)dst[i]=video_mix(first[i],last[i],ty.last_weight);
    }
}
void ui_draw_begin(uint32_t *p) {pixels=p;}
void ui_draw_close(void) {unsigned i;ui_image_reset();free(background);background=NULL;free(settings_background);settings_background=NULL;pixels=NULL;for(i=0;i<64;++i)free(surfaces[i].data);memset(surfaces,0,sizeof(surfaces));surface_bytes=0;surface_clock=0;}
