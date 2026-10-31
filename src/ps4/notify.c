/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#include "notify.h"
#include "psmtp/mtp.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#ifdef PSMTP_TEST_NOTIFY
static int32_t sceKernelSendNotificationRequest(int32_t,void *,size_t,int32_t);
#else
#include <orbis/libkernel.h>
#endif
/* Native notification request wire layout; no extra module or worker. */
typedef struct {
    int32_t type,request,priority,message_id,target,user,unknown[5];
    uint8_t icon;char text[1024],icon_uri[1024],reserved[1024];
} notification;
typedef char notification_abi[(sizeof(notification)==3120 && offsetof(notification,text)==45)?1:-1];
int psmtp_notify(const char *text,FILE *log){
    notification request;size_t n;int32_t rc;
    if(!text || !*text)return MTP_STATE;n=strlen(text);if(n>=sizeof(request.text))return MTP_LIMIT;
    memset(&request,0,sizeof(request));request.target=-1;memcpy(request.text,text,n+1);
    rc=sceKernelSendNotificationRequest(0,(void *)&request,sizeof(request),0);
    /* Notification failure never cancels an otherwise valid operation. Do not
     * log the text: these events deliberately contain no user filenames. */
    if(log)fprintf(log,"stage=system-notification status=0x%08x bytes=%zu blocking=0\n",(unsigned)rc,n);
    return rc;
}
