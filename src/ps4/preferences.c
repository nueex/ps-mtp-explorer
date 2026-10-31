/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#include "preferences.h"
#include "file_store.h"
#include "psmtp/ui_format.h"
#include <errno.h>
#include <string.h>
#ifdef PSMTP_TEST_PREFERENCES
#include "ps4_preferences_test_api.h"
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#define PREFERENCE_BYTES 256u
void psmtp_preferences_defaults(psmtp_preferences *p){memset(p,0,sizeof(*p));}
static int valid(const psmtp_preferences *p){
    return p && (p->list_view==0 || p->list_view==1) && p->sort_mode>=0 &&
        p->sort_mode<PSMTP_SORT_COUNT && (p->speed_boost==0 || p->speed_boost==1) && (p->install_mode==PSMTP_INSTALL_FAST || p->install_mode==PSMTP_INSTALL_PHONE_DIRECT);
}
static int field(const char **cursor,const char *key,unsigned limit,unsigned *value){
    const char *p=*cursor;size_t n=strlen(key);
    if(strncmp(p,key,n))return MTP_PROTOCOL;p+=n;
    /* Current schema values are single digits. Reject signs, overflow and
     * extra digits instead of relying on scanf's integer overflow behavior. */
    if(*p<'0' || (unsigned)(*p-'0')>limit)return MTP_PROTOCOL;
    *value=(unsigned)(*p++-'0');if(*p=='\r')++p;
    if(*p=='\n')++p;else if(*p)return MTP_PROTOCOL;
    *cursor=p;return 0;
}
static int decode(psmtp_preferences *p,char *text,size_t size){
    unsigned version,list,sort,boost,mode=0;const char *cursor=text;psmtp_preferences next;
    if(memchr(text,0,size))return MTP_PROTOCOL;text[size]=0;
    if(field(&cursor,"version=",2,&version) || (version!=1 && version!=2) ||
       field(&cursor,"list_view=",1,&list) || field(&cursor,"sort_mode=",PSMTP_SORT_COUNT-1,&sort) ||
       field(&cursor,"speed_boost=",1,&boost))return MTP_PROTOCOL;
    if(version==2 && field(&cursor,"install_mode=",1,&mode))return MTP_PROTOCOL;if(*cursor)return MTP_PROTOCOL;
    next.list_view=(int)list;next.sort_mode=(int)sort;next.speed_boost=(int)boost;next.install_mode=(int)mode;*p=next;return 0;
}
int psmtp_preferences_load(psmtp_preferences *p,FILE *log){
    char bytes[PREFERENCE_BYTES+2];size_t length=0;int directory=-1,fd=-1,rc=0,error=0;psmtp_native_stat st;
    psmtp_preferences_defaults(p);
    directory=psmtp_native_directory("/",PSMTP_PREFERENCES_DIRECTORY);
    if(directory<0){if(errno==ENOENT)goto done;rc=MTP_FILESYSTEM;goto done;}
    fd=open(PSMTP_PREFERENCES_PATH,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
    if(fd<0){if(errno==ENOENT)goto done;rc=MTP_FILESYSTEM;goto done;}
    if(psmtp_native_status(fd,&st)){rc=MTP_FILESYSTEM;goto done;}
    if(!S_ISREG(st.st_mode) || st.st_size<=0 || st.st_size>PREFERENCE_BYTES){rc=MTP_PROTOCOL;goto done;}
    while(length<=PREFERENCE_BYTES){ssize_t n=read(fd,bytes+length,PREFERENCE_BYTES+1-length);
        if(n<0){if(errno==EINTR)continue;rc=MTP_FILESYSTEM;goto done;}if(!n)break;length+=(size_t)n;
    }
    if(length>PREFERENCE_BYTES || (int64_t)length!=st.st_size){rc=MTP_PROTOCOL;goto done;}
    rc=decode(p,bytes,length);
done:
    error=rc==MTP_FILESYSTEM?errno:0;
    if(fd>=0 && close(fd) && !rc){rc=MTP_FILESYSTEM;error=errno;}
    if(directory>=0 && close(directory) && !rc){rc=MTP_FILESYSTEM;error=errno;}
    if(rc)psmtp_preferences_defaults(p);
    if(log)fprintf(log,"stage=preferences-load status=%d errno=%d view=%d sort=%d speed-boost=%d installation-mode=%d\n",rc,error,p->list_view,p->sort_mode,p->speed_boost,p->install_mode);
    errno=error;return rc;
}
int psmtp_preferences_save(const psmtp_preferences *p,FILE *log){
    char bytes[PREFERENCE_BYTES];int directory=-1,fd=-1,created=0,rc=MTP_FILESYSTEM,error=0,n;size_t written=0;
    if(!valid(p))return MTP_STATE;
    n=snprintf(bytes,sizeof(bytes),"version=2\nlist_view=%d\nsort_mode=%d\nspeed_boost=%d\ninstall_mode=%d\n",p->list_view,p->sort_mode,p->speed_boost,p->install_mode);
    if(n<0 || (size_t)n>=sizeof(bytes))return MTP_STATE;
    directory=psmtp_native_directory("/",PSMTP_PREFERENCES_DIRECTORY);
    if(directory<0 && errno==ENOENT){
        if(mkdir(PSMTP_PREFERENCES_DIRECTORY,0755) && errno!=EEXIST)goto done;
        directory=psmtp_native_directory("/",PSMTP_PREFERENCES_DIRECTORY);
    }
    if(directory<0)goto done;
    /* This reserved sibling is ours, including after a system kill. unlink
     * never follows a stale symlink. Never truncate the published file. */
    if(unlink(PSMTP_PREFERENCES_TEMP) && errno!=ENOENT)goto done;
    fd=open(PSMTP_PREFERENCES_TEMP,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0666);
    if(fd<0)goto done;created=1;
    if(fchmod(fd,0666))goto done;
    while(written<(size_t)n){ssize_t part=write(fd,bytes+written,(size_t)n-written);
        if(part<0){if(errno==EINTR)continue;goto done;}if(!part){errno=EIO;goto done;}written+=(size_t)part;}
    if(fsync(fd))goto done;
    {int status=close(fd);fd=-1;if(status)goto done;}
    if(rename(PSMTP_PREFERENCES_TEMP,PSMTP_PREFERENCES_PATH))goto done;created=0;
    if(fsync(directory))goto done;
    rc=0;
done:
    error=rc?errno:0;
    if(fd>=0 && close(fd) && !rc){rc=MTP_FILESYSTEM;error=errno;}
    if(created && unlink(PSMTP_PREFERENCES_TEMP) && log)fprintf(log,"stage=preferences-temp-cleanup errno=%d\n",errno);
    if(directory>=0 && close(directory) && !rc){rc=MTP_FILESYSTEM;error=errno;}
    if(log)fprintf(log,"stage=preferences-save status=%d errno=%d view=%d sort=%d speed-boost=%d\n",rc,error,p->list_view,p->sort_mode,p->speed_boost);
    errno=error;return rc;
}
