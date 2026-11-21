/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PSMTP_IME_H
#define PSMTP_IME_H
#include <stdio.h>
#include <stddef.h>
/* Native dialog owns separate UTF-16 buffers until Term succeeds. */
int psmtp_ime_text(const char *,char *,size_t,int (*)(void *),void *,FILE *);
int psmtp_ime_secret(const char *,char *,size_t,int (*)(void *),void *,FILE *);
int psmtp_ime_busy(void);
int psmtp_ime_shutdown(FILE *);
#endif
