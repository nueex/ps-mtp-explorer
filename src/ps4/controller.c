/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#include "controller.h"
#ifdef PSMTP_TEST_CONTROLLER
#include "ps4_controller_test_api.h"
#else
#include <orbis/libkernel.h>
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include "monotonic.h"
#endif
#include <string.h>
#include <inttypes.h>
/* UI-thread owner only. No worker reads these handles or this input state. */
#include "psmtp/cleanup_log.h"

static struct {int pad,started,initialized,user_owned,connected,seed,failures;int32_t error;uint32_t down;uint64_t retry_at,repeat_at;FILE *log;} input={.pad=-1};
static uint64_t input_now(void){struct timespec ts={0};return psmtp_monotonic_time(&ts)?0:(uint64_t)ts.tv_sec*1000+(uint64_t)ts.tv_nsec/1000000;}
static void input_retry(uint64_t now){
    int32_t user=-1,rc;OrbisUserServiceInitializeParams params={ORBIS_KERNEL_PRIO_FIFO_LOWEST};
    input.retry_at=now+500;
    if(!input.initialized){rc=scePadInit();if(rc){input.error=rc;return;}input.initialized=1;
        rc=sceUserServiceInitialize(&params);if(!rc)input.user_owned=1;}
    if(input.pad>=0)return;
    rc=sceUserServiceGetInitialUser(&user);if(rc){input.error=rc;return;}
    input.pad=scePadOpen(user,0,0,NULL);input.seed=1;
    if(input.log)fprintf(input.log,"stage=pad-reconnect-open rc=%d retry-ms=500\n",input.pad);
    if(input.pad<0)input.error=input.pad;
}
void psmtp_controller_open(FILE *log){input.log=log;input.started=1;input.seed=1;input_retry(input_now());}
uint32_t psmtp_controller_buttons(void){
    OrbisPadData pad;uint64_t now=input_now();uint32_t held,nav,pressed;int32_t rc;
    if(!input.started || now<input.retry_at)return 0;
    if(input.pad<0){input_retry(now);return 0;}
    memset(&pad,0,sizeof(pad));rc=scePadReadState(input.pad,&pad);
    if(rc<0){input.error=rc;input.connected=0;input.down=0;input.seed=1;input.retry_at=now+500;
        if(++input.failures>=3){int32_t close_rc=scePadClose(input.pad);if(input.log)fprintf(input.log,"stage=pad-reconnect-close rc=0x%08" PRIx32 "\n",(uint32_t)close_rc);if(!close_rc)input.pad=-1;input.failures=0;}
        return 0;
    }
    input.error=0;input.failures=0;
    if(!pad.connected){input.connected=0;input.down=0;input.seed=1;input.retry_at=now+500;return 0;}
    input.connected=1;input.retry_at=0;held=pad.buttons;
    if(pad.leftStick.y<64)held|=ORBIS_PAD_BUTTON_UP;
    if(pad.leftStick.y>192)held|=ORBIS_PAD_BUTTON_DOWN;
    nav=held&(ORBIS_PAD_BUTTON_UP|ORBIS_PAD_BUTTON_DOWN|ORBIS_PAD_BUTTON_LEFT|ORBIS_PAD_BUTTON_RIGHT|ORBIS_PAD_BUTTON_L1|ORBIS_PAD_BUTTON_R1);
    /* A button held while reconnecting must not become a fresh cancel press. */
    if(input.seed){input.seed=0;input.down=held;input.repeat_at=now+360;return 0;}
    pressed=held&~input.down;
    if(pressed&nav)input.repeat_at=now+360;
    else if(nav && now>=input.repeat_at){pressed|=nav;input.repeat_at=now+90;}
    input.down=held;return pressed;
}
uint32_t psmtp_controller_down(void){return input.down;}
int psmtp_controller_ready(void){return input.started;}
int psmtp_controller_connected(void){return input.started && input.pad>=0 && input.connected && !input.error;}
int32_t psmtp_controller_error(void){return input.error;}
int psmtp_controller_shutdown(FILE *log,int release_user){
    int32_t rc,first=0;input.started=0;input.connected=0;input.down=0;
    if(input.pad>=0){rc=scePadClose(input.pad);if(log)fprintf(log,"stage=pad-close rc=0x%08" PRIx32 "\n",(uint32_t)rc);if(!rc)input.pad=-1;else first=rc;}
    if(release_user && input.user_owned && input.pad<0){rc=sceUserServiceTerminate();if(log)fprintf(log,"stage=user-term rc=0x%08" PRIx32 "\n",(uint32_t)rc);if(!rc)input.user_owned=0;else if(!first)first=rc;}
    return first;
}

void psmtp_controller_close(FILE *log){(void)psmtp_controller_shutdown(log,1);}
