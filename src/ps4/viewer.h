/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PSMTP_VIEWER_H
#define PSMTP_VIEWER_H
#include "file_ops.h"
#include "work.h"

enum {PSMTP_FILE_UNKNOWN,PSMTP_FILE_TEXT,PSMTP_FILE_IMAGE,PSMTP_FILE_VIDEO,PSMTP_FILE_MUSIC};

typedef struct psmtp_viewer {
    psmtp_op_ref source;
    mtp_client *client;
    int kind,dirty,read_only,crlf;
    char staged_directory[PSMTP_LOCAL_PATH],staged_path[PSMTP_LOCAL_PATH];
    char *text;
    size_t length,lines,line,column;
    uint32_t *pixels;
    unsigned width,height;
    int zoom,pan_x,pan_y;
} psmtp_viewer;

int psmtp_viewer_kind(const char *);
int psmtp_stage_file(const psmtp_op_ref *,mtp_client *,char [PSMTP_LOCAL_PATH],char [PSMTP_LOCAL_PATH],psmtp_work *);
void psmtp_unstage_file(const char *,const char *);
int psmtp_viewer_load(void *,psmtp_work *);
void psmtp_viewer_close(psmtp_viewer *);
size_t psmtp_viewer_line(const psmtp_viewer *,size_t,size_t,char *,size_t);
void psmtp_viewer_scroll(psmtp_viewer *,int);
int psmtp_viewer_edit(psmtp_viewer *,const char *,int);
#endif
