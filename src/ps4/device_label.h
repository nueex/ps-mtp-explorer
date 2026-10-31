/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PSMTP_DEVICE_LABEL_H
#define PSMTP_DEVICE_LABEL_H
#include "ui_font.h"
#include <string.h>
#define PSMTP_DEVICE_LABEL_BYTES 96u
/* Preserve every byte, prefer word boundaries, never split a UTF-8 codepoint.
 * The location label already has a fixed 96-byte bound. */
static inline unsigned psmtp_device_label_lines(const char *name,char lines[96][96]){
    size_t offset=0,length=0;unsigned count=0;while(length<95 && name[length])++length;
    while(offset<length && count<95){size_t n=0,space=0;
        while(offset+n<length){size_t unit=1;unsigned char c=(unsigned char)name[offset+n];
            if((c&0xe0)==0xc0)unit=2;else if((c&0xf0)==0xe0)unit=3;else if((c&0xf8)==0xf0)unit=4;
            if(unit>length-offset-n)unit=1;
            memcpy(lines[count]+n,name+offset+n,unit);lines[count][n+unit]=0;
            if(n && psmtp_font_width(lines[count],26)>212)break;
            n+=unit;if(name[offset+n-1]==' ')space=n;
        }
        if(offset+n<length && space)n=space;
        if(!n)n=1;memcpy(lines[count],name+offset,n);lines[count][n]=0;offset+=n;++count;
    }
    if(!count){lines[0][0]=0;count=1;}return count;
}
#endif
