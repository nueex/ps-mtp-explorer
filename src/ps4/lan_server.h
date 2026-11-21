/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PSMTP_LAN_SERVER_H
#define PSMTP_LAN_SERVER_H
#include "work.h"
#include "psmtp/lan_pair.h"
typedef struct psmtp_lan_server psmtp_lan_server;
int psmtp_lan_net_acquire(int (*)(void));
void psmtp_lan_net_release(void (*)(void));
int psmtp_lan_open(psmtp_lan_server **,psmtp_work *,char address[16],unsigned char qr[PSMTP_LAN_QR_BYTES],
    psmtp_lan_pair_ui *);
int psmtp_lan_accept(psmtp_lan_server *);
int psmtp_lan_read(psmtp_lan_server *,void *,size_t);
int psmtp_lan_poll_read(psmtp_lan_server *,void *,size_t,int *);
int psmtp_lan_write(psmtp_lan_server *,const void *,size_t);
int psmtp_lan_close(psmtp_lan_server *);
void psmtp_lan_forget(void);
#endif
