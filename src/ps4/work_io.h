/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PSMTP_WORK_IO_H
#define PSMTP_WORK_IO_H
struct psmtp_work;
typedef struct {struct psmtp_work *work;int (*flush)(void *),(*validate)(void *);void *user;} psmtp_work_io_scope;
#if defined(PSMTP_TEST_FILE_SYSTEM) || defined(PSMTP_TEST_INSTALL_NATIVE)
static inline void psmtp_work_bind_io(psmtp_work_io_scope *s,int (*flush)(void *),int (*validate)(void *),void *user){s->work=0;(void)flush;(void)validate;(void)user;}
static inline void psmtp_work_unbind_io(psmtp_work_io_scope *s){(void)s;}
#else
void psmtp_work_bind_io(psmtp_work_io_scope *,int (*)(void *),int (*)(void *),void *);
void psmtp_work_unbind_io(psmtp_work_io_scope *);
#endif
#endif
