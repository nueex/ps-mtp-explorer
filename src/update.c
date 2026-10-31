/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#include "psmtp/update.h"
#include "psmtp/version.h"
#include <string.h>
#include <ctype.h>
typedef struct {const char *at,*end;} json;
static void white(json *j){while(j->at<j->end && (*j->at==' ' || *j->at=='\t' || *j->at=='\n' || *j->at=='\r'))++j->at;}
static int token(json *j,char c){white(j);if(j->at==j->end || *j->at!=c)return 0;++j->at;return 1;}
static int string(json *j,char *out,size_t cap){size_t n=0;if(!token(j,'"'))return 0;
    while(j->at<j->end){unsigned char c=(unsigned char)*j->at++;if(c=='"'){out[n]=0;return 1;}
        if(c=='\\'){if(j->at==j->end)return 0;c=(unsigned char)*j->at++;if(c!='/' && c!='\\' && c!='"')return 0;}
        if(c<32 || c>=127 || n+1>=cap)return 0;out[n++]=(char)c;
    }return 0;
}
static int integer(json *j,uint64_t *out){uint64_t v=0;const char *begin;white(j);begin=j->at;
    while(j->at<j->end && *j->at>='0' && *j->at<='9'){unsigned d=(unsigned)(*j->at++-'0');if(v>(UINT64_MAX-d)/10)return 0;v=v*10+d;}
    if(j->at==begin || (j->at-begin>1 && *begin=='0'))return 0;*out=v;return 1;
}
int psmtp_update_version(const char *s){size_t n=s?strlen(s):0;unsigned major=0;size_t dot=n==4?1:2;
    if((n!=4 && n!=5) || s[dot]!='.')return -1;
    for(size_t i=0;i<n;++i){if(i==dot)continue;if(s[i]<'0' || s[i]>'9')return -1;if(i<dot)major=major*10+(unsigned)(s[i]-'0');}
    return (int)(major*100+(unsigned)(s[dot+1]-'0')*10+(unsigned)(s[dot+2]-'0'));
}
int psmtp_update_https(const char *s){const char *host,*path;size_t n=s?strlen(s):0;
    if(n<10 || n>=2048 || memcmp(s,"https://",8))return 0;host=s+8;path=strchr(host,'/');if(!path || path==host || path-host>253)return 0;
    for(const char *p=host;p<path;++p)if(!isalnum((unsigned char)*p) && *p!='-' && *p!='.')return 0;
    if(*host=='.' || *host=='-' || path[-1]=='.' || path[-1]=='-')return 0;
    for(const char *p=path;*p;++p){unsigned char c=(unsigned char)*p;if(c<=32 || c>=127 || c=='#' || c=='\\' || c=='"')return 0;}
    return 1;
}
int psmtp_update_newer(const psmtp_update_manifest *m,const char *current){int a,b;if(!m || !m->enabled)return 0;a=psmtp_update_version(m->version);b=psmtp_update_version(current);return a>=0 && b>=0 && a>b;}
int psmtp_update_parse(const char *bytes,size_t length,psmtp_update_manifest *out){
    static const char *const keys[]={"schema","enabled","version","title_id","content_id","size","sha256","url"};
    json j;psmtp_update_manifest next={0};unsigned seen=0;char key[32],value[80];uint64_t v;
    if(!bytes || !out || !length || length>PSMTP_UPDATE_MANIFEST_LIMIT || memchr(bytes,0,length))return MTP_PROTOCOL;j.at=bytes;j.end=bytes+length;
    if(!token(&j,'{'))return MTP_PROTOCOL;
    for(;;){unsigned field;white(&j);if(j.at<j.end && *j.at=='}'){++j.at;break;}
        if(!string(&j,key,sizeof(key)) || !token(&j,':'))return MTP_PROTOCOL;
        for(field=0;field<8 && strcmp(key,keys[field]);++field){}if(field==8 || (seen&(1u<<field)))return MTP_PROTOCOL;seen|=1u<<field;
        if(field==0 || field==5){if(!integer(&j,&v))return MTP_PROTOCOL;if(field==0){if(v!=1)return MTP_UNSUPPORTED;}else next.size=v;}
        else if(field==1){white(&j);if(j.end-j.at>=4 && !memcmp(j.at,"true",4)){next.enabled=1;j.at+=4;}else if(j.end-j.at>=5 && !memcmp(j.at,"false",5))j.at+=5;else return MTP_PROTOCOL;}
        else if(field==2){if(!string(&j,next.version,sizeof(next.version)) || psmtp_update_version(next.version)<0)return MTP_PROTOCOL;}
        else if(field==7){if(!string(&j,next.url,sizeof(next.url)) || !psmtp_update_https(next.url))return MTP_PROTOCOL;}
        else {if(!string(&j,value,sizeof(value)))return MTP_PROTOCOL;
            if(field==3 && strcmp(value,PSMTP_TITLE_ID))return MTP_VERIFY;
            if(field==4 && strcmp(value,PSMTP_UPDATE_CONTENT_ID))return MTP_VERIFY;
            if(field==6){if(strlen(value)!=64)return MTP_PROTOCOL;for(unsigned i=0;i<32;++i){unsigned byte=0;for(unsigned k=0;k<2;++k){unsigned char c=(unsigned char)value[i*2+k];if(c>='0' && c<='9')c-='0';else if(c>='a' && c<='f')c=c-'a'+10;else if(c>='A' && c<='F')c=c-'A'+10;else return MTP_PROTOCOL;byte=byte*16+c;}next.sha256[i]=(uint8_t)byte;}}
        }
        white(&j);if(j.at<j.end && *j.at=='}'){++j.at;break;}if(!token(&j,','))return MTP_PROTOCOL;white(&j);if(j.at<j.end && *j.at=='}')return MTP_PROTOCOL;
    }
    white(&j);if(j.at!=j.end || (seen&3)!=3)return MTP_PROTOCOL;
    if(next.enabled && (seen!=255 || next.size<0x2000 || next.size>PSMTP_UPDATE_MAX_BYTES))return MTP_PROTOCOL;
    *out=next;return 0;
}
