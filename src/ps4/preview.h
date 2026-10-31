/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#ifndef PSMTP_PREVIEW_H
#define PSMTP_PREVIEW_H
#include "psmtp/metadata.h"
#include "psmtp/navigator.h"
#include "psmtp/locations.h"
#include "file_store.h"
#ifdef PSMTP_TEST_EXPLORER
static void psmtp_preview_open(FILE *f){(void)f;}
static int psmtp_preview_close(FILE *f){(void)f;
#ifdef PSMTP_TEST_PREVIEW_CLOSE_STATUS
return PSMTP_TEST_PREVIEW_CLOSE_STATUS;
#else
return 0;
#endif
}
static void psmtp_preview_stop(void){}
static int psmtp_preview_quiesce(void){return 0;}
static void psmtp_preview_frame(const psmtp_location *l,const mtp_navigator *n,const psmtp_local_browser *b,int v,int a,uint64_t t){(void)l;(void)n;(void)b;(void)v;(void)a;(void)t;}
static const psmtp_metadata *psmtp_preview_lookup(void *u,size_t i){(void)u;(void)i;return NULL;}
static int psmtp_preview_phone_busy(void){return 0;}
static void psmtp_preview_cancel(void){}
static void psmtp_preview_invalidate(void){}
static uint64_t psmtp_preview_revision(void){return 0;}
#else
void psmtp_preview_open(FILE *);
/* A failed drain retains the mailbox/client and must lead to system exit. */
int psmtp_preview_close(FILE *);
void psmtp_preview_stop(void);
int psmtp_preview_quiesce(void);
void psmtp_preview_frame(const psmtp_location *,const mtp_navigator *,const psmtp_local_browser *,int,int,uint64_t);
const psmtp_metadata *psmtp_preview_lookup(void *,size_t);
int psmtp_preview_phone_busy(void);
void psmtp_preview_cancel(void);
void psmtp_preview_invalidate(void);
/* UI-owned stamp changes on cache invalidation or result publication. */
uint64_t psmtp_preview_revision(void);
#endif
#endif
