/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PSMTP_PHONE_INSTALL_SERVER_H
#define PSMTP_PHONE_INSTALL_SERVER_H
#include "psmtp/mtp.h"
#include <stdio.h>
typedef struct psmtp_phone_server psmtp_phone_server;
typedef int (*psmtp_phone_control)(void *,int *,unsigned *);
typedef struct {void *user;mtp_client *client;uint64_t size;int (*read)(void *,uint64_t,uint8_t *,size_t);int (*validate)(void *);psmtp_phone_control control;void *control_user;size_t chunk_bytes;} psmtp_phone_source;
/* Optional, dynamically resolved network APIs. Bind loopback only; 4 HTTP
 * consumers share one serialized MTP transport and bounded chunk buffers. */
int psmtp_phone_server_open(psmtp_phone_server **,const psmtp_phone_source *,char *,size_t,FILE *);
int psmtp_phone_server_error(psmtp_phone_server *);
unsigned psmtp_phone_server_requests(psmtp_phone_server *);
/* Drain bounded request traces on the installer owner, never on HTTP threads. */
void psmtp_phone_server_log(psmtp_phone_server *,FILE *);
void psmtp_phone_server_stop(psmtp_phone_server *,int);
/* On failure, *server remains owned/retained; never free a live reader. */
int psmtp_phone_server_close(psmtp_phone_server **,int,FILE *);
#endif
