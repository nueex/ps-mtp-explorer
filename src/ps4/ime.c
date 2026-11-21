/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#include "ime.h"
#include "psmtp/mtp.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef PSMTP_TEST_IME
#include "ps4_ime_test_api.h"
#else
#include <orbis/libkernel.h>
#include <orbis/Sysmodule.h>
#include <orbis/UserService.h>
#endif
typedef struct {
    uint32_t user,type;uint64_t languages;uint32_t enter,input;void *filter;
    uint32_t option,maximum;uint16_t *text;float x,y;int32_t horizontal,vertical;
    const uint16_t *placeholder,*title;uint8_t reserved[16];
} ime_parameter;
typedef struct {int32_t end;uint8_t reserved[12];} ime_result;
typedef char ime_layout[(sizeof(ime_parameter)==96 && offsetof(ime_parameter,maximum)==36 && offsetof(ime_parameter,text)==40 && offsetof(ime_parameter,title)==72 && sizeof(ime_result)==16)?1:-1];
#include "psmtp/cleanup_log.h"

static struct ime_context {
    int owned,started;uint16_t text[256],title[64];ime_parameter parameter;
    int32_t (*init)(const ime_parameter *,void *),(*status)(void),(*result)(ime_result *),(*term)(void);
    void (*abort_dialog)(void);
} *ime;
static int encode(const char *source,uint16_t *out,size_t capacity){
    const unsigned char *p=(const unsigned char *)source;size_t length=strlen(source),used=0,at=0;
    while(at<length){uint32_t cp=p[at++];unsigned extra=0;
        if(cp>=128){if(cp>=0xc2 && cp<=0xdf){extra=1;cp&=31;}else if(cp>=0xe0 && cp<=0xef){extra=2;cp&=15;}else if(cp>=0xf0 && cp<=0xf4){extra=3;cp&=7;}else return MTP_PROTOCOL;
            if(extra>length-at)return MTP_PROTOCOL;
            for(unsigned i=0;i<extra;++i){if((p[at]&0xc0)!=0x80)return MTP_PROTOCOL;cp=(cp<<6)|(p[at++]&63);}
            if((extra==1 && cp<128) || (extra==2 && cp<2048) || (extra==3 && cp<65536) || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff))return MTP_PROTOCOL;}
        if(used+(cp>=0x10000?2u:1u)>=capacity)return MTP_LIMIT;
        if(cp>=0x10000){out[used++]=(uint16_t)(0xd800+((cp-0x10000)>>10));cp=0xdc00+((cp-0x10000)&1023);}
        out[used++]=(uint16_t)cp;
    }out[used]=0;return 0;
}
static int decode(const uint16_t *source,size_t units,char *out,size_t capacity){
    size_t at=0,used=0;
    while(at<units && source[at]){uint32_t cp=source[at++];unsigned n;
        if(cp>=0xd800 && cp<=0xdbff){if(at>=units || source[at]<0xdc00 || source[at]>0xdfff)return MTP_PROTOCOL;cp=0x10000+((cp-0xd800)<<10)+(source[at++]-0xdc00);}
        else if(cp>=0xdc00 && cp<=0xdfff)return MTP_PROTOCOL;
        n=cp<128?1:cp<2048?2:cp<65536?3:4;if(used+n>=capacity)return MTP_LIMIT;
        if(n==1)out[used++]=(char)cp;
        else {out[used++]=(char)((n==2?0xc0:n==3?0xe0:0xf0)|(cp>>(6*(n-1))));
            for(unsigned i=n-1;i;--i)out[used++]=(char)(0x80|((cp>>(6*(i-1)))&63));}
    }
    if(at==units)return MTP_PROTOCOL;out[used]=0;return 0;
}
static int module_id(void){
    OrbisKernelModule ids[128];size_t count=0;int found=-1;
    memset(ids,255,sizeof(ids));
    if(sceKernelGetModuleList(ids,128,&count) || count>128)return -1;
    for(size_t i=0;i<count;++i){OrbisKernelModuleInfo info;const char *leaf;size_t n=0;
        if(ids[i]>INT32_MAX)return -1;memset(&info,0,sizeof(info));info.size=sizeof(info);if(sceKernelGetModuleInfo(ids[i],&info))return -1;
        while(n<sizeof(info.name) && info.name[n])++n;if(n==sizeof(info.name))return -1;
        leaf=strrchr(info.name,'/');leaf=leaf?leaf+1:info.name;
        if(!strcmp(leaf,"libSceImeDialog.sprx") || !strcmp(leaf,"libSceImeDialog.prx") || !strcmp(leaf,"libSceImeDialog")){if(found>=0 || ids[i]>INT32_MAX)return -1;found=(int)ids[i];}
    }return found;
}
static int resolve(int module,const char *name,void *target,size_t size){
    void *address=NULL;if(module<0 || size!=sizeof(address) || sceKernelDlsym(module,name,&address) || !address)return MTP_UNSUPPORTED;
    memcpy(target,&address,size);return 0;
}
#define IME_SYMBOL(field,name) resolve(module,name,&ime->field,sizeof(ime->field))
static int initialize(FILE *log){
    int module,rc;
    if(ime)return ime->started?MTP_STATE:ime->init && ime->status && ime->result && ime->term && ime->abort_dialog?0:MTP_UNSUPPORTED;
    ime=calloc(1,sizeof(*ime));if(!ime)return MTP_MEMORY;
    if(sceSysmoduleIsLoaded(ORBIS_SYSMODULE_IME_DIALOG)){
        rc=sceSysmoduleLoadModule(ORBIS_SYSMODULE_IME_DIALOG);
        if(log)fprintf(log,"stage=ime-module status=0x%08x\n",(unsigned)rc);
        if(rc){free(ime);ime=NULL;return MTP_UNSUPPORTED;}ime->owned=1;
    }
    module=module_id();
    if(IME_SYMBOL(init,"sceImeDialogInit") || IME_SYMBOL(status,"sceImeDialogGetStatus") ||
       IME_SYMBOL(result,"sceImeDialogGetResult") || IME_SYMBOL(term,"sceImeDialogTerm") || IME_SYMBOL(abort_dialog,"sceImeDialogAbort")){
        (void)psmtp_ime_shutdown(log);return MTP_UNSUPPORTED;
    }return 0;
}
int psmtp_ime_busy(void){return ime && ime->started;}
static int finish(FILE *log){
    int32_t rc=ime->term();if(log)fprintf(log,"stage=ime-term status=0x%08x buffers-retained=%d\n",(unsigned)rc,rc!=0);
    if(!rc)ime->started=0;return rc?MTP_IO:0;
}
int psmtp_ime_shutdown(FILE *log){
    int32_t rc;if(!ime)return 0;
    if(ime->started){ime->abort_dialog();
        for(unsigned i=0;i<1000 && ime->started;++i){if(ime->status()!=1 && !finish(NULL))break;sceKernelUsleep(1000);}
        if(log)fprintf(log,"stage=ime-drain status=%d buffers-retained=%d\n",ime->started?MTP_TIMEOUT:0,ime->started);
        if(ime->started)return MTP_TIMEOUT;
    }
    if(ime->owned){rc=sceSysmoduleUnloadModule(ORBIS_SYSMODULE_IME_DIALOG);if(rc){if(log)fprintf(log,"stage=ime-unload status=0x%08x\n",(unsigned)rc);return MTP_IO;}}
    free(ime);ime=NULL;return 0;
}
static void clear_secret(void *value,size_t size){
    volatile unsigned char *bytes=value;
    while(size--)*bytes++=0;
}
static int ime_input(const char *title,char *text,size_t capacity,int (*pump)(void *),void *user,FILE *log,int secret){
    char result[256];int rc,running=0,last_state=-1;unsigned startup_polls=0,polls=0;int32_t native,user_id;ime_result status;
    if(!title || !text || capacity<2 || capacity>sizeof(result) || !memchr(text,0,capacity))return MTP_STATE;
    if(pump && pump(user))return MTP_CANCELLED;
    rc=initialize(log);if(rc)return rc;
    memset(&ime->parameter,0,sizeof(ime->parameter));memset(ime->text,0,sizeof(ime->text));memset(ime->title,0,sizeof(ime->title));
    rc=encode(title,ime->title,64);if(rc)return rc;
    rc=encode(text,ime->text,255);
    if(rc){ime->text[0]=0;if(log)fprintf(log,"stage=ime-prefill-cleared status=%d names-in-log=0\n",rc);}

    native=sceUserServiceGetInitialUser(&user_id);
    if(native || user_id<0){if(secret)clear_secret(ime->text,sizeof(ime->text));return MTP_IO;}
    ime->parameter.user=(uint32_t)user_id;ime->parameter.maximum=254;ime->parameter.text=ime->text;ime->parameter.title=ime->title;
    if(secret){
        /* The native dialog rejects Password with the default input type. */
        ime->parameter.type=1; /* BasicLatin */
        ime->parameter.option=4u|32u; /* Password, no learning. */
    }
    ime->parameter.x=960.0f;ime->parameter.y=540.0f;ime->parameter.horizontal=1;ime->parameter.vertical=1;
    native=ime->init(&ime->parameter,NULL);if(log)fprintf(log,"stage=ime-init status=0x%08x max-utf16-units=254 centre=960,540 alignment=1,1 names-in-log=0\n",(unsigned)native);
    if(native){if(secret)clear_secret(ime->text,sizeof(ime->text));return MTP_IO;}
    ime->started=1;
    for(;;){int current=ime->status();++polls;
        if(current!=last_state){if(log)fprintf(log,"stage=ime-state status=%d poll=%u system-input-owner=1\n",current,polls);last_state=current;}
        if(current==2){memset(&status,0,sizeof(status));native=ime->result(&status);
            rc=native?MTP_IO:status.end?MTP_CANCELLED:decode(ime->text,256,result,capacity);
            if(log)fprintf(log,"stage=ime-result native=0x%08x end=%d result=%d names-in-log=0\n",(unsigned)native,status.end,rc);
            if(finish(log)){if(secret)clear_secret(result,sizeof(result));return MTP_IO;}
            if(!rc){
                if(secret)clear_secret(text,capacity);
                memcpy(text,result,strlen(result)+1);
            }
            if(secret){clear_secret(result,sizeof(result));clear_secret(ime->text,sizeof(ime->text));}
            return rc;
        }
        /* The system dialog can enter RUNNING asynchronously. Keep checking
         * exit during a bounded initial NONE interval; the system owns input. */
        if(current==1)running=1;
        if((current!=1 && !(current==0 && !running && startup_polls++<64)) || (pump && pump(user))){
            rc=current==1 || current==0?MTP_CANCELLED:MTP_IO;
            if(current==0 && startup_polls>=64)rc=MTP_IO;
            if(log)fprintf(log,"stage=ime-status status=%d startup-polls=%u running-seen=%d result=%d\n",current,startup_polls,running,rc);
            if(psmtp_ime_shutdown(log))return MTP_TIMEOUT;return rc;
        }
        sceKernelUsleep(16000);
    }
}
int psmtp_ime_text(const char *title,char *text,size_t size,int (*pump)(void *),void *user,FILE *log){
    return ime_input(title,text,size,pump,user,log,0);
}
int psmtp_ime_secret(const char *title,char *text,size_t size,int (*pump)(void *),void *user,FILE *log){
    return ime_input(title,text,size,pump,user,log,1);
}
