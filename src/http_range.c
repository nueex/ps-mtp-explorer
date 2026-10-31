/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#include "psmtp/http_range.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
static int equal(const char *a,size_t n,const char *b){size_t i;if(n!=strlen(b))return 0;for(i=0;i<n;++i){unsigned c=(unsigned char)a[i];if(c>='A' && c<='Z')c+='a'-'A';if(c!=(unsigned char)b[i])return 0;}return 1;}
static const char *eol(const char *p,const char *end){while(end-p>=2){if(p[0]=='\r' && p[1]=='\n')return p;++p;}return NULL;}
static int decimal(const char **at,const char *end,uint64_t *out){const char *p=*at;uint64_t v=0;if(p==end || *p<'0' || *p>'9')return -1;
    do{unsigned d=(unsigned)(*p-'0');if(v>(UINT64_MAX-d)/10)return -1;v=v*10+d;++p;}while(p<end && *p>='0' && *p<='9');*at=p;*out=v;return 0;}
/* HTTP clients may send absolute-form targets or append a query. The query
 * does not select a different MTP object. Absolute-form is accepted only for
 * loopback authority; no request can make this server fetch a remote URL. */
static int target(const char **begin,const char **end){const char *p=*begin,*last=*end,*query;
    if(memchr(p,'#',(size_t)(last-p)))return -1;
    if(last-p>=7 && equal(p,4,"http") && !memcmp(p+4,"://",3)){
        const char *host=p+7,*slash=memchr(host,'/',(size_t)(last-host)),*colon;uint64_t port;
        if(!slash)return -1;colon=memchr(host,':',(size_t)(slash-host));
        if(!equal(host,(size_t)((colon?colon:slash)-host),"127.0.0.1") && !equal(host,(size_t)((colon?colon:slash)-host),"localhost"))return -1;
        if(colon){const char *number=colon+1;if(decimal(&number,slash,&port) || number!=slash || !port || port>65535)return -1;}p=slash;
    }
    if(p==last || *p!='/')return -1;query=memchr(p,'?',(size_t)(last-p));*begin=p;*end=query?query:last;return 0;
}
int psmtp_http_parse(const char *s,size_t n,const char *path,uint64_t size,psmtp_http_request *r){
    const char *end,*line,*p,*space,*range=NULL,*range_end=NULL;int seen_range=0,ignore_range=0;size_t i;
    memset(r,0,sizeof(*r));r->status=400;r->total=size;
    if(n>PSMTP_HTTP_HEADER_LIMIT){r->status=431;return 0;}
    end=s+n;
    if(n<4 || memchr(s,0,n) || memcmp(end-4,"\r\n\r\n",4))return 0;
    for(i=0;i<n;++i)if(((unsigned char)s[i]<32 && s[i]!='\r' && s[i]!='\n' && s[i]!='\t') || (unsigned char)s[i]==127)return 0;
    line=eol(s,end);if(!line)return 0;space=memchr(s,' ',(size_t)(line-s));if(!space)return 0;
    if(space-s==4 && !memcmp(s,"HEAD",4))r->head=1;
    else if(space-s!=3 || memcmp(s,"GET",3)){r->status=405;return 0;}
    p=space+1;space=memchr(p,' ',(size_t)(line-p));if(!space)return 0;
    if((size_t)(line-space-1)!=8 || (memcmp(space+1,"HTTP/1.1",8) && memcmp(space+1,"HTTP/1.0",8)))return 0;
    {const char *last=space;if(target(&p,&last))return 0;
    if((size_t)(last-p)==strlen(path)+7 && !memcmp(p,path,strlen(path)) && !memcmp(p+strlen(path),"/health",7))r->health=1;
    else if((size_t)(last-p)!=strlen(path) || memcmp(p,path,strlen(path))){r->status=404;return 0;}}
    p=line+2;
    while(p<end-2){const char *colon,*value,*last;line=eol(p,end);if(!line)return 0;colon=memchr(p,':',(size_t)(line-p));if(!colon || colon==p || *p==' ' || *p=='\t')return 0;
        value=colon+1;while(value<line && (*value==' ' || *value=='\t'))++value;last=line;while(last>value && (last[-1]==' ' || last[-1]=='\t'))--last;
        if(equal(p,(size_t)(colon-p),"range")){if(seen_range++)return 0;range=value;range_end=last;}
        if(equal(p,(size_t)(colon-p),"if-range"))ignore_range=1;
        if(equal(p,(size_t)(colon-p),"transfer-encoding"))return 0;
        if(equal(p,(size_t)(colon-p),"content-length") && (last-value!=1 || *value!='0'))return 0;
        p=line+2;
    }
    if(p!=end-2)return 0;
    r->status=200;r->length=r->health?0:size;
    if(!range || r->head || r->health || ignore_range)return 0;
    if(range_end-range<6 || !equal(range,5,"bytes") || range[5]!='=' || memchr(range,',',(size_t)(range_end-range)))return 0;
    p=range+6;
    if(p<range_end && *p=='-'){uint64_t tail;++p;if(decimal(&p,range_end,&tail) || p!=range_end || !tail || !size){r->status=416;r->length=0;return 0;}if(tail>size)tail=size;r->offset=size-tail;r->length=tail;}
    else {uint64_t start,last;if(decimal(&p,range_end,&start) || p==range_end || *p++!='-' || start>=size){r->status=416;r->length=0;return 0;}
        last=size-1;if(p<range_end && decimal(&p,range_end,&last)){r->status=416;r->length=0;return 0;}
        if(p!=range_end || last<start){r->status=416;r->length=0;return 0;}if(last>=size)last=size-1;r->offset=start;r->length=last-start+1;
    }r->status=206;return 0;
}
int psmtp_http_response(char *out,size_t cap,const psmtp_http_request *r){char range[160]="";const char *reason=r->status==200?"OK":r->status==206?"Partial Content":r->status==404?"Not Found":r->status==405?"Method Not Allowed":r->status==416?"Range Not Satisfiable":r->status==431?"Request Header Fields Too Large":"Bad Request";int n;
    if(r->status==206)snprintf(range,sizeof(range),"Content-Range: bytes %" PRIu64 "-%" PRIu64 "/%" PRIu64 "\r\n",r->offset,r->offset+r->length-1,r->total);
    else if(r->status==416)snprintf(range,sizeof(range),"Content-Range: bytes */%" PRIu64 "\r\n",r->total);
    n=snprintf(out,cap,"HTTP/1.1 %d %s\r\nConnection: close\r\nContent-Type: application/octet-stream\r\nAccept-Ranges: bytes\r\nContent-Length: %" PRIu64 "\r\n%s%s%s\r\n",r->status,reason,r->status==200 || r->status==206?r->length:UINT64_C(0),range,r->health?"X-PSMTP-Bridge: ready\r\n":"",r->status==405?"Allow: GET, HEAD\r\n":"");return n<0 || (size_t)n>=cap?-1:n;
}
