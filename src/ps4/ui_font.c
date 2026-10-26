/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#include "ui_font.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <stb_truetype.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#include "font_data.inc"
#define GLYPHS 768
typedef struct { uint32_t code; int px,w,h,x,y,advance; uint64_t used; unsigned char *alpha; } glyph;
static struct { stbtt_fontinfo info; glyph cache[GLYPHS]; uint64_t clock; int ready; } font;
static unsigned quick[2048];
static uint32_t next(const unsigned char **text) {
    const unsigned char *p=*text; uint32_t c; unsigned n,i;
    if(!*p) return 0;
    c=*p++;
    if(c<128) { *text=p; return c; }
    if(c>=0xc2 && c<=0xdf) { c&=31; n=1; }
    else if(c>=0xe0 && c<=0xef) { c&=15; n=2; }
    else if(c>=0xf0 && c<=0xf4) { c&=7; n=3; }
    else { *text=p; return 0xfffd; }
    for(i=0;i<n;++i) { if((p[i]&0xc0)!=0x80) { *text=p; return 0xfffd; } c=(c<<6)|(p[i]&63); }
    *text=p+n;
    if((n==1 && c<0x80) || (n==2 && c<0x800) || (n==3 && c<0x10000) || c>0x10ffff || (c>=0xd800 && c<=0xdfff)) return 0xfffd;
    return c;
}
int psmtp_font_init(void) {
    if(font.ready) return 0;
    if(!stbtt_InitFont(&font.info,psmtp_font_data,0)) return -1;
    font.ready=1; return 0;
}
void psmtp_font_close(void) {
    unsigned i; for(i=0;i<GLYPHS;++i) free(font.cache[i].alpha);
    memset(&font,0,sizeof(font));
    memset(quick,0,sizeof(quick));
}
static glyph *lookup(uint32_t code,int px) {
    unsigned i,slot=0,hash=(code*31u+(unsigned)px*131u)%2048; float scale; int advance,left,index;
    glyph *g;
    if(!font.ready && psmtp_font_init()) return NULL;
    if(quick[hash]) {g=&font.cache[quick[hash]-1];if(g->used && g->code==code && g->px==px){g->used=++font.clock;return g;}}
    for(i=0;i<GLYPHS;++i) {
        if(font.cache[i].used && font.cache[i].code==code && font.cache[i].px==px) { font.cache[i].used=++font.clock;quick[hash]=i+1; return &font.cache[i]; }
        if(font.cache[i].used<font.cache[slot].used) slot=i;
    }
    g=&font.cache[slot]; free(g->alpha); memset(g,0,sizeof(*g));
    index=stbtt_FindGlyphIndex(&font.info,(int)code);
    if(!index) index=stbtt_FindGlyphIndex(&font.info,0xfffd);
    scale=stbtt_ScaleForMappingEmToPixels(&font.info,(float)px);
    stbtt_GetGlyphHMetrics(&font.info,index,&advance,&left);
    g->alpha=stbtt_GetGlyphBitmap(&font.info,scale,scale,index,&g->w,&g->h,&g->x,&g->y);
    g->advance=(int)(advance*scale+0.5f); g->code=code; g->px=px; g->used=++font.clock;
    quick[hash]=slot+1;
    return g;
}
int psmtp_font_width(const char *s,int px) {
    const unsigned char *p=(const unsigned char *)s; int width=0; uint32_t c;
    while((c=next(&p))) { glyph *g=lookup(c,px); if(g) width+=g->advance; }
    return width;
}
static uint32_t blend(uint32_t dst,uint32_t src,unsigned a) {
    unsigned inv=255-a;
    return 0xff000000u|((((dst&255)*inv+(src&255)*a+127)/255))|
        (((((dst>>8)&255)*inv+((src>>8)&255)*a+127)/255)<<8)|
        (((((dst>>16)&255)*inv+((src>>16)&255)*a+127)/255)<<16);
}
void psmtp_font_draw(uint32_t *frame,int x,int y,int px,int max_width,const char *s,uint32_t color) {
    const unsigned char *p=(const unsigned char *)s; uint32_t c; int start=x,ellipsis=psmtp_font_width("...",px),truncate=psmtp_font_width(s,px)>max_width;
    while((c=next(&p))) {
        glyph *g=lookup(c,px); int i,j,baseline=y+px;
        if(!g) return;
        if(x-start+g->advance>max_width) return;
        if(truncate && *p && strcmp(s,"...") && x-start+g->advance+ellipsis>max_width) { psmtp_font_draw(frame,x,y,px,max_width-(x-start),"...",color); return; }
        for(j=0;j<g->h;++j) { int yy=baseline+g->y+j; if(yy<0 || yy>=1080) continue;
            for(i=0;i<g->w;++i) { int xx=x+g->x+i; unsigned a;
                if(xx<0 || xx>=1920 || xx>=start+max_width) continue;
                a=g->alpha?g->alpha[j*g->w+i]:0;
                if(a) frame[yy*1920+xx]=blend(frame[yy*1920+xx],color,a);
            }
        }
        x+=g->advance;
    }
}
