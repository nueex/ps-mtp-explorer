/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#include "system_dialog.h"
#include "psmtp/mtp.h"
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#ifndef PSMTP_TEST_SYSTEM_DIALOG
#include "optional_module.h"
#else
static int32_t sceKernelUsleep(unsigned);
#endif
#include "psmtp/cleanup_log.h"
/* C-compatible native ABI; the installed SDK uses C++ enum-underlying syntax. */
typedef struct {size_t size;uint8_t reserved[36];uint32_t magic;} dialog_base;
typedef struct {const char *first,*second;uint8_t reserved[32];} dialog_buttons;
typedef struct {int32_t type,pad;const char *text;dialog_buttons *buttons;uint8_t reserved[24];} dialog_user;
typedef struct {dialog_base base;size_t size;int32_t mode,pad;dialog_user *user;void *progress,*system;int32_t user_id;uint8_t reserved[40];int32_t pad2;} dialog_parameter;
typedef struct {int32_t mode,result,button;uint8_t reserved[32];} dialog_result;
typedef char dialog_abi[(sizeof(dialog_base)==48 && sizeof(dialog_parameter)==136 && offsetof(dialog_parameter,user)==64 && sizeof(dialog_user)==48 && sizeof(dialog_result)==44)?1:-1];
typedef struct {
    int common_owned,module_owned,initialized,started;dialog_parameter parameter;dialog_user user;dialog_buttons buttons;char text[512];
    int32_t (*common_init)(void),(*init)(void),(*open)(const dialog_parameter *),(*status)(void),(*result)(dialog_result *),(*close)(void),(*term)(void);
} system_dialog;
static system_dialog *dialog;
#ifdef PSMTP_TEST_SYSTEM_DIALOG
static int dialog_load(system_dialog *);
static int dialog_unload(system_dialog *);
#else
static int dialog_load(system_dialog *d){int common,module;
    common=psmtp_optional_module("libSceCommonDialog");if(common<0){if(sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_COMMON_DIALOG))return MTP_UNSUPPORTED;d->common_owned=1;common=psmtp_optional_module("libSceCommonDialog");}
    module=psmtp_optional_module("libSceMsgDialog");if(module<0){if(sceSysmoduleLoadModule(ORBIS_SYSMODULE_MESSAGE_DIALOG))return MTP_UNSUPPORTED;d->module_owned=1;module=psmtp_optional_module("libSceMsgDialog");}
#define DG(m,field,name) do{if(psmtp_optional_symbol(m,name,&d->field,sizeof(d->field)))return MTP_UNSUPPORTED;}while(0)
    DG(common,common_init,"sceCommonDialogInitialize");DG(module,init,"sceMsgDialogInitialize");DG(module,open,"sceMsgDialogOpen");DG(module,status,"sceMsgDialogUpdateStatus");
    DG(module,result,"sceMsgDialogGetResult");DG(module,close,"sceMsgDialogClose");DG(module,term,"sceMsgDialogTerminate");
#undef DG
    return 0;
}
static int dialog_unload(system_dialog *d){
    if(d->module_owned){if(sceSysmoduleUnloadModule(ORBIS_SYSMODULE_MESSAGE_DIALOG))return MTP_IO;d->module_owned=0;}
    if(d->common_owned){if(sceSysmoduleUnloadModuleInternal(ORBIS_SYSMODULE_INTERNAL_COMMON_DIALOG))return MTP_IO;d->common_owned=0;}return 0;
}
#endif
int psmtp_system_dialog_busy(void){return dialog!=NULL;}
int psmtp_system_dialog_shutdown(FILE *log){int32_t rc=0;if(!dialog)return 0;
    if(dialog->started){(void)dialog->close();for(unsigned i=0;i<100;++i){int state=dialog->status();if(state!=2 && state!=1){dialog->started=0;break;}sceKernelUsleep(1000);}if(dialog->started)return MTP_TIMEOUT;}
    if(dialog->initialized){rc=dialog->term();if(rc){if(log)fprintf(log,"stage=update-dialog-term native=0x%08x retained=1\n",(unsigned)rc);return MTP_IO;}dialog->initialized=0;}
    rc=dialog_unload(dialog);if(rc)return rc;free(dialog);dialog=NULL;return 0;
}
int psmtp_system_dialog(const char *message,int update,int (*pump)(void *),void *user,FILE *log){
    int rc,running=0;unsigned startup=0;dialog_result result={0};
    if(!message || strlen(message)>=512 || dialog)return MTP_STATE;if(pump && pump(user))return MTP_CANCELLED;
    dialog=calloc(1,sizeof(*dialog));if(!dialog)return MTP_MEMORY;
    rc=dialog_load(dialog);if(rc)goto done;
    rc=dialog->common_init();if(rc)goto done;rc=dialog->init();if(rc)goto done;dialog->initialized=1;
    memcpy(dialog->text,message,strlen(message)+1);dialog->parameter.base.size=sizeof(dialog_base);
    dialog->parameter.base.magic=(uint32_t)(UINT32_C(0xc0d1a109)+(uintptr_t)&dialog->parameter.base);
    dialog->parameter.size=sizeof(dialog_parameter);dialog->parameter.mode=1;dialog->parameter.user=&dialog->user;
    dialog->user.text=dialog->text;dialog->user.type=update?9:0;
    if(update){dialog->buttons.first="Update";dialog->buttons.second="Cancel";dialog->user.buttons=&dialog->buttons;}
    rc=dialog->open(&dialog->parameter);if(log)fprintf(log,"stage=update-native-dialog update=%d native=0x%08x\n",update,(unsigned)rc);if(rc)goto done;dialog->started=1;
    for(;;){int state=dialog->status();
        if(state==3){rc=dialog->result(&result);dialog->started=0;if(!rc)rc=!result.result && result.button==1?1:0;else rc=MTP_IO;break;}
        if(state==2)running=1;
        if((state!=2 && !((state==0 || state==1) && !running && startup++<64)) || (pump && pump(user))){rc=startup>=64?MTP_TIMEOUT:state>=0 && state<=2?MTP_CANCELLED:MTP_IO;break;}
        sceKernelUsleep(16000);
    }
done:
    if(rc>1)rc=MTP_IO;
    if(psmtp_system_dialog_shutdown(log))return MTP_TIMEOUT;return rc;
}
