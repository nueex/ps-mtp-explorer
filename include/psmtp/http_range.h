/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PSMTP_HTTP_RANGE_H
#define PSMTP_HTTP_RANGE_H
#include <stdint.h>
#include <stddef.h>
#define PSMTP_HTTP_HEADER_LIMIT 16384u
typedef struct {int status,head,health;uint64_t offset,length,total;} psmtp_http_request;
/* One immutable resource, one request/connection. No request-body or path access.
 * Origin-form and loopback absolute-form targets may have opaque queries.
 * Multiple ranges, unknown units and If-Range fall back to a full 200 response. */
int psmtp_http_parse(const char *,size_t,const char *,uint64_t,psmtp_http_request *);
int psmtp_http_response(char *,size_t,const psmtp_http_request *);
#endif
