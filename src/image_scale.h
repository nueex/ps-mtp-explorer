/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PSMTP_IMAGE_SCALE_H
#define PSMTP_IMAGE_SCALE_H

#include <stddef.h>
#include <stdint.h>

enum { PSMTP_SAMPLE_ONE=256 };
typedef struct {
    unsigned first,last,first_weight,last_weight;
} psmtp_image_taps;

/* Integrate the source footprint when shrinking. When enlarging, sample
 * between source pixel centers and extend the edge pixels to the bounds. */
static inline psmtp_image_taps psmtp_image_linear_axis(unsigned index,unsigned source,unsigned target) {
    uint64_t center=((uint64_t)index*2+1)*source*(PSMTP_SAMPLE_ONE/2)/target;
    if(center<=PSMTP_SAMPLE_ONE/2)return (psmtp_image_taps){0,0,PSMTP_SAMPLE_ONE,0};
    center-=PSMTP_SAMPLE_ONE/2;
    unsigned first=(unsigned)(center/PSMTP_SAMPLE_ONE),fraction=(unsigned)(center%PSMTP_SAMPLE_ONE);
    if(!fraction || first>=source-1)return (psmtp_image_taps){first,first,PSMTP_SAMPLE_ONE,0};
    return (psmtp_image_taps){first,first+1,PSMTP_SAMPLE_ONE-fraction,fraction};
}

static inline psmtp_image_taps psmtp_image_axis(unsigned index,unsigned source,unsigned target) {
    if(source<=target)return psmtp_image_linear_axis(index,source,target);
    uint64_t start=(uint64_t)index*source*PSMTP_SAMPLE_ONE/target;
    uint64_t end=((uint64_t)index+1)*source*PSMTP_SAMPLE_ONE/target;
    unsigned first=(unsigned)(start/PSMTP_SAMPLE_ONE),last=(unsigned)((end-1)/PSMTP_SAMPLE_ONE);
    if(first==last)return (psmtp_image_taps){first,first,(unsigned)(end-start),0};
    return (psmtp_image_taps){first,last,PSMTP_SAMPLE_ONE-(unsigned)(start%PSMTP_SAMPLE_ONE),
        (unsigned)(end-(uint64_t)last*PSMTP_SAMPLE_ONE)};
}

static inline unsigned psmtp_image_weight(psmtp_image_taps taps,unsigned index) {
    return index==taps.first?taps.first_weight:index==taps.last?taps.last_weight:PSMTP_SAMPLE_ONE;
}

/* Interpolate premultiplied components to keep transparent RGB from
 * producing fringes. Signed strides also support rotated/reflected sources. */
static inline uint32_t psmtp_image_sample(const unsigned char *source,ptrdiff_t step_x,ptrdiff_t step_y,
                                          psmtp_image_taps tx,psmtp_image_taps ty) {
    const unsigned char *row=source+(ptrdiff_t)ty.first*step_y+(ptrdiff_t)tx.first*step_x;
    if(tx.first==tx.last && ty.first==ty.last)
        return (uint32_t)row[3]<<24 | (uint32_t)row[2]<<16 | (uint32_t)row[1]<<8 | row[0];
    uint64_t red=0,green=0,blue=0,alpha=0,weight=0;
    for(unsigned y=ty.first;y<=ty.last;++y){
        const unsigned char *pixel=row;
        unsigned wy=psmtp_image_weight(ty,y);
        for(unsigned x=tx.first;x<=tx.last;++x){
            unsigned w=wy*psmtp_image_weight(tx,x),a=pixel[3]*w;
            weight+=w;
            alpha+=a;
            red+=pixel[0]*a;
            green+=pixel[1]*a;
            blue+=pixel[2]*a;
            if(x<tx.last)pixel+=step_x;
        }
        if(y<ty.last)row+=step_y;
    }
    if(!alpha)return 0;
    return (uint32_t)((alpha+weight/2)/weight)<<24 |
           (uint32_t)((blue+alpha/2)/alpha)<<16 |
           (uint32_t)((green+alpha/2)/alpha)<<8 |
           (uint32_t)((red+alpha/2)/alpha);
}
#endif
