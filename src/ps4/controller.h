/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PSMTP_CONTROLLER_H
#define PSMTP_CONTROLLER_H
#include <stdint.h>
#include <stdio.h>
void psmtp_controller_open(FILE *);
uint32_t psmtp_controller_buttons(void);
uint32_t psmtp_controller_down(void);
int psmtp_controller_ready(void);
int psmtp_controller_connected(void);
int32_t psmtp_controller_error(void);
void psmtp_controller_close(FILE *);
int psmtp_controller_shutdown(FILE *,int);
#endif
