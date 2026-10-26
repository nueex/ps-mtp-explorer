/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#include "psmtp/pkg.h"
#include "psmtp/sha256.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <ctype.h>
static uint32_t pkg32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static uint64_t pkg64(const uint8_t *p){return (uint64_t)pkg32(p)<<32|pkg32(p+4);}
int psmtp_pkg_name(const char *name){size_t n=name?strlen(name):0;return n>=4 && name[n-4]=='.' && tolower((unsigned char)name[n-3])=='p' && tolower((unsigned char)name[n-2])=='k' && tolower((unsigned char)name[n-1])=='g';}
int psmtp_pkg_inspect(const psmtp_pkg_source *s,psmtp_pkg_info *p){
    uint8_t h[0x440];unsigned i;uint64_t table,count;psmtp_sha256 hash;int rc;
    memset(p,0,sizeof(*p));if(!s || !s->read || s->size<0x2000 || s->size>INT64_MAX)return MTP_UNSUPPORTED;
    rc=s->read(s->user,0,h,sizeof(h));if(rc)return rc;
    if(memcmp(h,"\177CNT",4) || pkg64(h+0x430)!=s->size)return MTP_VERIFY;
    table=pkg32(h+0x18);count=pkg32(h+0x10);if(!count || table<0x440 || table>s->size || count>(s->size-table)/32)return MTP_VERIFY;
    for(i=0;i<36;++i){unsigned c=h[0x40+i];if(i==6 || i==19){if(c!='-')return MTP_VERIFY;}else if(i==16){if(c!='_')return MTP_VERIFY;}else if(!((c>='A' && c<='Z') || (c>='0' && c<='9') || (i>=20 && c=='_')))return MTP_VERIFY;}
    if(h[0x40+36])return MTP_VERIFY;
    memcpy(p->content_id,h+0x40,36);memcpy(p->title_id,h+0x47,9);
    p->size=s->size;p->type=pkg32(h+0x74);p->flags=pkg32(h+0x78);
    if(p->type==0x1a)strcpy(p->package_type,"PS4GD");else if(p->type==0x1b)strcpy(p->package_type,"PS4AC");else if(p->type==0x1c)strcpy(p->package_type,"PS4AL");else if(p->type==0x1e)strcpy(p->package_type,"PS4DP");else return MTP_UNSUPPORTED;
    p->patch=(p->flags&UINT32_C(0x60100000))!=0 || p->type==0x1e;
    p->install_bytes=pkg64(h+0x418);if(p->install_bytes<p->size)p->install_bytes=p->size;if(p->install_bytes>INT64_MAX)return MTP_LIMIT;
    psmtp_sha256_init(&hash);psmtp_sha256_update(&hash,h,sizeof(h));psmtp_sha256_final(&hash,p->header_digest);return 0;
}
static int lower_equal(const char *p,size_t n,const char *s){size_t i;if(strlen(s)!=n)return 0;for(i=0;i<n;++i)if(tolower((unsigned char)p[i])!=s[i])return 0;return 1;}
static int number(const char **p,const char *end,uint64_t *v){int any=0;*v=0;while(*p<end && **p>='0' && **p<='9'){unsigned d=(unsigned)(*(*p)++-'0');if(*v>(UINT64_MAX-d)/10)return 0;*v=*v*10+d;any=1;}return any;}
static const char *line_end(const char *p,const char *end){while(end-p>=2){if(p[0]=='\r' && p[1]=='\n')return p;++p;}return NULL;}
static int http_error(psmtp_pkg_send send,void *user,int code,uint64_t size){char h[256];int n=snprintf(h,sizeof(h),"HTTP/1.1 %d Error\r\nContent-Length: 0\r\nContent-Range: bytes */%" PRIu64 "\r\nConnection: close\r\n\r\n",code,size);return send(user,(const uint8_t *)h,(size_t)n);}
int psmtp_pkg_http(const psmtp_pkg_source *s,const char *request,size_t length,const char *path,psmtp_pkg_send send,void *user){
    const char *end,*line,*next,*at,*colon;uint64_t first=0,last,n;int head=0,ranged=0,rc;char header[512];uint8_t *data;size_t k;
    if(!send || !s || !request || !path || !s->read)return MTP_STATE;end=request+length;last=s->size?s->size-1:0;
    if(!s->size || length<4 || length>8192 || memchr(request,0,length) || memcmp(end-4,"\r\n\r\n",4))return http_error(send,user,400,s->size);
    line=request;next=line_end(line,end);if(!next)return http_error(send,user,400,s->size);
    if(!strncmp(line,"HEAD ",5)){head=1;at=line+5;}else if(!strncmp(line,"GET ",4))at=line+4;else return http_error(send,user,405,s->size);


    if(next-at>=7 && !memcmp(at,"http://",7)){
        const char *authority=at+7,*slash=memchr(authority,'/',(size_t)(next-authority));
        if(!slash || slash-authority<9 || memcmp(authority,"127.0.0.1",9))return http_error(send,user,404,s->size);
        if(slash-authority>9){const char *port=authority+9;uint64_t value;
            if(*port++!=':' || !number(&port,slash,&value) || port!=slash || !value || value>65535)return http_error(send,user,404,s->size);}
        at=slash;
    }
    k=strlen(path);if((size_t)(next-at)<k+9 || memcmp(at,path,k))return http_error(send,user,404,s->size);
    at+=k;
    if(at<next && *at=='?'){while(at<next && *at!=' '){unsigned char c=(unsigned char)*at;if(c<0x21 || c>0x7e || c=='#')return http_error(send,user,400,s->size);++at;}}
    if(at==next || *at!=' ')return http_error(send,user,404,s->size);
    ++at;if(next-at!=8 || (memcmp(at,"HTTP/1.1",8) && memcmp(at,"HTTP/1.0",8)))return http_error(send,user,400,s->size);
    for(line=next+2;line<end-2;line=next+2){
        next=line_end(line,end);if(!next)return http_error(send,user,400,s->size);colon=memchr(line,':',(size_t)(next-line));if(!colon)return http_error(send,user,400,s->size);
        at=colon+1;while(at<next && (*at==' ' || *at=='\t'))++at;
        if(lower_equal(line,(size_t)(colon-line),"range")){
            if(ranged++ || next-at<6 || memcmp(at,"bytes=",6))return http_error(send,user,416,s->size);at+=6;
            if(at<next && *at=='-'){++at;if(!number(&at,next,&n) || !n)return http_error(send,user,416,s->size);first=n>=s->size?0:s->size-n;}
            else {if(!number(&at,next,&first) || at==next || *at++!='-' || first>=s->size)return http_error(send,user,416,s->size);
                if(at<next){if(!number(&at,next,&last) || last<first)return http_error(send,user,416,s->size);if(last>=s->size)last=s->size-1;}}
            while(at<next && (*at==' ' || *at=='\t'))++at;if(at!=next)return http_error(send,user,416,s->size);
        }else if(lower_equal(line,(size_t)(colon-line),"transfer-encoding") || (lower_equal(line,(size_t)(colon-line),"content-length") && (next-at!=1 || *at!='0')))return http_error(send,user,400,s->size);
    }
    n=last-first+1;
    k=(size_t)snprintf(header,sizeof(header),"HTTP/1.1 %s\r\nContent-Type: application/octet-stream\r\nContent-Length: %" PRIu64 "\r\nAccept-Ranges: bytes\r\nConnection: close\r\n",ranged?"206 Partial Content":"200 OK",n);
    if(ranged)k+=(size_t)snprintf(header+k,sizeof(header)-k,"Content-Range: bytes %" PRIu64 "-%" PRIu64 "/%" PRIu64 "\r\n",first,last,s->size);
    memcpy(header+k,"\r\n",2);k+=2;rc=send(user,(const uint8_t *)header,k);if(rc || head)return rc;
    data=malloc(MTP_STREAM_CHUNK);if(!data)return MTP_MEMORY;
    while(n){k=n>MTP_STREAM_CHUNK?MTP_STREAM_CHUNK:(size_t)n;rc=s->read(s->user,first,data,k);if(rc)break;rc=send(user,data,k);if(rc)break;first+=k;n-=k;}
    free(data);return rc;
}
