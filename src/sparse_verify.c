/* PS MTP Explorer - Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "psmtp/sparse_verify.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

int psmtp_verify_workspace_init(psmtp_verify_workspace *w) {
    w->left=malloc(2u*PSMTP_VERIFY_WINDOW);w->right=w->left?w->left+PSMTP_VERIFY_WINDOW:NULL;return w->left?0:ENOMEM;
}
void psmtp_verify_workspace_free(psmtp_verify_workspace *w) {free(w->left);w->left=w->right=NULL;}
size_t psmtp_verify_plan(uint64_t size,psmtp_verify_window windows[5]) {
    size_t i;if(size<PSMTP_VERIFY_SMALL){windows[0].offset=0;windows[0].length=size;return size?1:0;}
    windows[0].offset=0;
    windows[1].offset=(size/4)&~UINT64_C(4095);
    windows[2].offset=(size/2)&~UINT64_C(4095);
    windows[3].offset=((size/4)*3+(size%4)*3/4)&~UINT64_C(4095);
    windows[4].offset=(size-PSMTP_VERIFY_WINDOW)&~UINT64_C(4095);
    for(i=0;i<5;++i)windows[i].length=PSMTP_VERIFY_WINDOW;
    /* Include the real EOF: rounding the tail start down adds at most 4095
     * padding bytes. Each individual read is still capped at 64 KiB. Without
     * this padding an unaligned file's final bytes would never be sampled. */
    windows[4].length=size-windows[4].offset;return 5;
}
static enum psmtp_verify_status verify_read(const psmtp_verify_source *s,uint64_t offset,uint8_t *data,size_t length,psmtp_verify_cancel cancel,void *user,uint64_t done,uint64_t total) {
    size_t used=0;while(used<length){size_t got=0;int rc;
        if(cancel && cancel(user,done,total))return PSMTP_VERIFY_CANCELLED;
        rc=s->read_at(s->user,offset+used,data+used,length-used,&got);
        if(rc==EINTR)continue;if(rc)return PSMTP_VERIFY_IO_ERROR;
        if(!got)return PSMTP_VERIFY_MISMATCH;if(got>length-used)return PSMTP_VERIFY_IO_ERROR;used+=got;
    }return PSMTP_VERIFY_OK;
}
enum psmtp_verify_status psmtp_sparse_verify(const psmtp_verify_source *a,const psmtp_verify_source *b,psmtp_verify_workspace *work,psmtp_verify_cancel cancel,void *user,uint64_t *compared) {
    uint64_t as=0,bs=0,expected,done=0,total=0;size_t count,i;psmtp_verify_window windows[5];enum psmtp_verify_status status;
    if(compared)*compared=0;
    if(!a || !b || !a->size || !b->size || !a->read_at || !b->read_at || !work || !work->left || !work->right)return PSMTP_VERIFY_IO_ERROR;
    if(cancel && cancel(user,0,0))return PSMTP_VERIFY_CANCELLED;
    if(a->size(a->user,&as) || b->size(b->user,&bs))return PSMTP_VERIFY_IO_ERROR;
    if(as!=bs)return PSMTP_VERIFY_MISMATCH;
    if(!as)return PSMTP_VERIFY_OK;
    expected=as;
    count=psmtp_verify_plan(as,windows);for(i=0;i<count;++i)total+=windows[i].length;
    /* Fast probabilistic structural sanity check, not a full-file integrity
     * proof: corruption between windows and storage/OS cache masking can pass.
     * Native callers must flush, fsync, close and reopen O_RDONLY first. */
    for(i=0;i<count;++i){uint64_t at=0;
        while(at<windows[i].length){size_t n=(size_t)(windows[i].length-at>PSMTP_VERIFY_WINDOW?PSMTP_VERIFY_WINDOW:windows[i].length-at);
            if(cancel && cancel(user,done,total))return PSMTP_VERIFY_CANCELLED;
            status=verify_read(a,windows[i].offset+at,work->left,n,cancel,user,done,total);if(status!=PSMTP_VERIFY_OK)return status;
            status=verify_read(b,windows[i].offset+at,work->right,n,cancel,user,done,total);if(status!=PSMTP_VERIFY_OK)return status;
            if(memcmp(work->left,work->right,n))return PSMTP_VERIFY_MISMATCH;
            at+=n;done+=n;if(compared)*compared=done;
        }
    }
    /* Catch truncation/growth while sampling. Empty files need size equality only. */
    if(a->size(a->user,&as) || b->size(b->user,&bs))return PSMTP_VERIFY_IO_ERROR;
    if(as!=expected || bs!=expected)return PSMTP_VERIFY_MISMATCH;
    return cancel && cancel(user,done,total)?PSMTP_VERIFY_CANCELLED:PSMTP_VERIFY_OK;
}
int psmtp_verify_capture_init(psmtp_verify_capture *c,uint64_t size) {
    size_t i,n=0;memset(c,0,sizeof(*c));c->size=size;c->count=psmtp_verify_plan(size,c->windows);
    for(i=0;i<c->count;++i)n+=(size_t)c->windows[i].length;
    c->bytes=n?malloc(n):NULL;return n && !c->bytes?ENOMEM:0;
}
void psmtp_verify_capture_free(psmtp_verify_capture *c) {free(c->bytes);memset(c,0,sizeof(*c));}
int psmtp_verify_capture_feed(psmtp_verify_capture *c,uint64_t offset,const uint8_t *data,size_t n) {
    size_t i,base=0;if(offset!=c->consumed || offset>c->size || n>c->size-offset)return EINVAL;
    for(i=0;i<c->count;++i){uint64_t begin=c->windows[i].offset,end=begin+c->windows[i].length,lo=offset>begin?offset:begin,hi=offset+n<end?offset+n:end;
        if(lo<hi)memcpy(c->bytes+base+(size_t)(lo-begin),data+(size_t)(lo-offset),(size_t)(hi-lo));
        base+=(size_t)c->windows[i].length;
    }c->consumed+=n;return 0;
}
static int captured_size(void *user,uint64_t *size) {psmtp_verify_capture *c=user;if(c->consumed!=c->size)return EIO;*size=c->size;return 0;}
static int captured_read(void *user,uint64_t offset,uint8_t *data,size_t n,size_t *got) {
    psmtp_verify_capture *c=user;size_t i,base=0;*got=0;if(c->consumed!=c->size)return EIO;
    for(i=0;i<c->count;++i){uint64_t begin=c->windows[i].offset,length=c->windows[i].length;
        if(offset>=begin && offset-begin<length){uint64_t remaining=length-(offset-begin);if(n>remaining)n=(size_t)remaining;memcpy(data,c->bytes+base+(size_t)(offset-begin),n);*got=n;return 0;}base+=(size_t)length;
    }return offset==c->size?0:EINVAL;
}
psmtp_verify_source psmtp_verify_capture_source(psmtp_verify_capture *c) {psmtp_verify_source s={c,captured_size,captured_read};return s;}
