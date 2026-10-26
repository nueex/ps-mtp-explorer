/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#include "psmtp/mtp.h"
#include <stdlib.h>
#include "psmtp/sha256.h"
#include <string.h>

static uint16_t u16(const uint8_t *p) { return (uint16_t)(p[0] | (uint16_t)p[1] << 8); }
static uint32_t u32(const uint8_t *p) { return (uint32_t)u16(p) | (uint32_t)u16(p+2) << 16; }
static uint64_t u64(const uint8_t *p) { return (uint64_t)u32(p) | (uint64_t)u32(p+4) << 32; }
static void put16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void put32(uint8_t *p, uint32_t v) { put16(p,(uint16_t)v); put16(p+2,(uint16_t)(v>>16)); }

const char *mtp_error_string(int e) {
    switch(e) {
    case MTP_OK:return "ok"; case MTP_IO:return "transport error";
    case MTP_TIMEOUT:return "deadline/timeout"; case MTP_DISCONNECTED:return "disconnected";
    case MTP_PROTOCOL:return "malformed or unexpected protocol data";
    case MTP_LIMIT:return "explicit resource limit"; case MTP_MEMORY:return "allocation failed";
    case MTP_RESPONSE:return "device rejected operation"; case MTP_STATE:return "invalid session/stream state";
    case MTP_UNSUPPORTED:return "unsupported";
    case MTP_CANCELLED:return "cancelled"; case MTP_VERIFY:return "verification failed";
    case MTP_FILESYSTEM:return "filesystem error";
    case MTP_EXISTS:return "destination name already exists";
    case MTP_NO_SPACE:return "insufficient storage space";
    case MTP_SPACE_UNKNOWN:return "available storage space could not be checked";
    default:return "unknown error";
    }
}

int mtp_decode_string(const uint8_t *p, size_t n, char *out, size_t cap, size_t *used) {
    size_t count, i, j=0;
    if (!n || !cap) return MTP_PROTOCOL;
    count=p[0];
    if (1+count*2>n) return MTP_PROTOCOL;
    *used=1+count*2;
    if (count && u16(p+1+(count-1)*2)!=0) return MTP_PROTOCOL;
    for(i=0; i+1<count; ++i) {
        uint32_t c=u16(p+1+i*2);
        size_t bytes;
        if (!c) return MTP_PROTOCOL;
        if (c>=0xd800 && c<=0xdbff) {
            uint16_t lo;
            if (++i+1>=count) return MTP_PROTOCOL;
            lo=u16(p+1+i*2);
            if (lo<0xdc00 || lo>0xdfff) return MTP_PROTOCOL;
            c=0x10000+((c-0xd800)<<10)+(lo-0xdc00);
        } else if (c>=0xdc00 && c<=0xdfff) return MTP_PROTOCOL;
        bytes=c<0x80?1:c<0x800?2:c<0x10000?3:4;
        if (j+bytes>=cap) return MTP_LIMIT;
        if (bytes==1) out[j++]=(char)c;
        else {
            if(bytes==2) out[j++]=(char)(0xc0|(c>>6));
            if(bytes==3) { out[j++]=(char)(0xe0|(c>>12)); out[j++]=(char)(0x80|((c>>6)&63)); }
            if(bytes==4) { out[j++]=(char)(0xf0|(c>>18)); out[j++]=(char)(0x80|((c>>12)&63)); out[j++]=(char)(0x80|((c>>6)&63)); }
            out[j++]=(char)(0x80|(c&63));
        }
    }
    out[j]=0;
    return MTP_OK;
}

typedef struct { const uint8_t *p; size_t n; } cursor;
static int take(cursor *c, size_t n, const uint8_t **p) {
    if(n>c->n) return MTP_PROTOCOL;
    *p=c->p; c->p+=n; c->n-=n; return MTP_OK;
}
static int str(cursor *c, char *out) {
    size_t used=0; int e=mtp_decode_string(c->p,c->n,out,MTP_STRING_BYTES,&used);
    if(e) return e;
    c->p+=used; c->n-=used; return MTP_OK;
}
static int array16(cursor *c, uint16_t *out, size_t cap, size_t *count) {
    const uint8_t *p; uint32_t n; size_t i; int e;
    if((e=take(c,4,&p))) return e;
    n=u32(p); if(n>cap) return MTP_LIMIT;
    if((e=take(c,(size_t)n*2,&p))) return e;
    for(i=0;out && i<n;++i) out[i]=u16(p+i*2);
    if(count) *count=n;
    return MTP_OK;
}
#define TRY(x) do { int err_=(x); if(err_) return err_; } while(0)
int mtp_parse_device(const uint8_t *p, size_t n, mtp_device_info *v) {
    cursor c={p,n}; const uint8_t *q; char discarded[MTP_STRING_BYTES]; int i;
    memset(v,0,sizeof(*v)); TRY(take(&c,8,&q));
    v->standard_version=u16(q); v->vendor_extension=u32(q+2); v->vendor_version=u16(q+6);
    TRY(str(&c,v->vendor_description)); TRY(take(&c,2,&q)); v->functional_mode=u16(q);
    TRY(array16(&c,v->operations,MTP_MAX_OPS,&v->operation_count));
    for(i=0;i<4;++i) TRY(array16(&c,NULL,MTP_MAX_OPS,NULL));
    TRY(str(&c,v->manufacturer)); TRY(str(&c,v->model)); TRY(str(&c,v->version));
    TRY(str(&c,discarded));
    if(discarded[0]) {psmtp_sha256 hash;psmtp_sha256_init(&hash);psmtp_sha256_update(&hash,v->manufacturer,strlen(v->manufacturer)+1);psmtp_sha256_update(&hash,v->model,strlen(v->model)+1);psmtp_sha256_update(&hash,discarded,strlen(discarded));psmtp_sha256_final(&hash,v->identity);v->identity_valid=1;}
    memset(discarded,0,sizeof(discarded));
    return c.n?MTP_PROTOCOL:MTP_OK;
}
int mtp_parse_storage(const uint8_t *p,size_t n,mtp_storage_info *v) {
    cursor c={p,n}; const uint8_t *q;
    memset(v,0,sizeof(*v)); TRY(take(&c,26,&q));
    v->type=u16(q); v->filesystem=u16(q+2); v->access=u16(q+4);
    v->capacity=u64(q+6); v->free_bytes=u64(q+14); v->free_objects=u32(q+22);
    TRY(str(&c,v->description)); TRY(str(&c,v->label));
    return c.n?MTP_PROTOCOL:MTP_OK;
}
int mtp_parse_object(const uint8_t *p,size_t n,mtp_object_info *v) {
    cursor c={p,n}; const uint8_t *q; char discarded[MTP_STRING_BYTES];
    memset(v,0,sizeof(*v)); TRY(take(&c,52,&q));
    v->storage=u32(q); v->format=u16(q+4); v->protection=u16(q+6); v->size32=u32(q+8);
    v->image_width=u32(q+26);v->image_height=u32(q+30);
    v->parent=u32(q+38); v->association=u16(q+42); v->association_desc=u32(q+44); v->sequence=u32(q+48);
    TRY(str(&c,v->name)); TRY(str(&c,discarded)); TRY(str(&c,v->modified)); TRY(str(&c,discarded));
    return c.n?MTP_PROTOCOL:MTP_OK;
}
int mtp_parse_ids(const uint8_t *p,size_t n,uint32_t *ids,size_t cap,size_t *count) {
    uint32_t k; size_t i; *count=0;
    if(n<4) return MTP_PROTOCOL;
    k=u32(p); if(k>MTP_MAX_ITEMS || k>cap) return MTP_LIMIT;
    if(n!=4+(size_t)k*4) return MTP_PROTOCOL;
    for(i=0;i<k;++i) ids[i]=u32(p+4+i*4);
    *count=k; return MTP_OK;
}

void mtp_init(mtp_client *c,mtp_transport io) {
    memset(c,0,sizeof(*c)); c->io=io; c->timeout_ms=5000;c->out_max_packet=512;
}
static uint32_t remaining(mtp_client *c,uint64_t start) {
    uint64_t elapsed=c->io.now_ms(c->io.user)-start;
    return elapsed>=c->timeout_ms?0:c->timeout_ms-(uint32_t)elapsed;
}
static int exact_profile(mtp_client *c,uint8_t *out,size_t n,uint64_t start,mtp_download_stats *stats) {
    size_t got, k; unsigned zeros=0; int e; uint32_t ms;
    while(n) {
        if(c->rx_pos==c->rx_len) {
            ms=remaining(c,start); if(!ms) return MTP_TIMEOUT;
            got=0;
            {uint64_t before=stats?c->io.now_ms(c->io.user):0;
            e=c->io.read(c->io.user,c->rx,sizeof(c->rx),&got,ms);
            if(stats){uint64_t after=c->io.now_ms(c->io.user);++stats->read_calls;stats->read_bytes+=got;if(after>=before)stats->io_ms+=after-before;}}
            if(e) return e;
            if(!remaining(c,start)) return MTP_TIMEOUT;
            if(got>sizeof(c->rx)) return MTP_PROTOCOL;
            c->rx_pos=0; c->rx_len=got;
            if(!got) { if(++zeros>8) return MTP_TIMEOUT; continue; }
        }
        k=c->rx_len-c->rx_pos; if(k>n) k=n;
        memcpy(out,c->rx+c->rx_pos,k); c->rx_pos+=k; out+=k; n-=k;
    }
    return MTP_OK;
}
static int exact(mtp_client *c,uint8_t *out,size_t n,uint64_t start) {return exact_profile(c,out,n,start,NULL);}
static int container(mtp_client *c,uint64_t start,int response_only,uint16_t *type,uint16_t *code,uint8_t **payload,size_t *n) {
    uint8_t h[12]; uint32_t len; int e;
    *payload=NULL; *n=0;
    TRY(exact(c,h,12,start)); len=u32(h);
    if(len<12) return MTP_PROTOCOL;
    if(len>MTP_MAX_CONTAINER) return MTP_LIMIT;
    *type=u16(h+4); *code=u16(h+6);
    if(u32(h+8)!=c->last_tid) return MTP_PROTOCOL;
    if(*type!=2 && *type!=3) return MTP_PROTOCOL;
    if(response_only && *type!=3) return MTP_PROTOCOL;
    if(*type==2 && *code!=c->last_op) return MTP_PROTOCOL;
    if(*type==3 && ((*code&0xf000)!=0x2000 || len>32 || (len-12)%4)) return MTP_PROTOCOL;
    *n=len-12;
    if(*n) {
        *payload=(uint8_t *)malloc(*n); if(!*payload) return MTP_MEMORY;
        e=exact(c,*payload,*n,start);
        if(e) { free(*payload); *payload=NULL; return e; }
    }
    return MTP_OK;
}

static int transaction_ex(mtp_client *c,uint16_t op,const uint32_t *params,size_t np,int data,const uint8_t *outgoing,size_t outgoing_size,uint8_t **out,size_t *n) {
    uint8_t cmd[32], *p=NULL, *response=NULL; size_t size=0,rn=0,sent=0,i;
    uint16_t type=0,code=0; uint64_t start; int e,had_data;
    *out=NULL; *n=0;
    if(c->poisoned || !c->io.read || !c->io.write || !c->io.now_ms || np>5) return MTP_STATE;
    if(op!=0x1001 && op!=0x1002 && !c->session) return MTP_STATE;
    if(c->next_tid==UINT32_MAX) return MTP_LIMIT;
    if(c->rx_len!=c->rx_pos) { c->poisoned=1; return MTP_PROTOCOL; }
    c->last_tid=c->next_tid++; c->last_op=op; c->response=0; c->response_param_count=0;
    put32(cmd,(uint32_t)(12+np*4)); put16(cmd+4,1); put16(cmd+6,op); put32(cmd+8,c->last_tid);
    for(i=0;i<np;++i) put32(cmd+12+i*4,params[i]);
    start=c->io.now_ms(c->io.user);
    e=c->io.write(c->io.user,cmd,12+np*4,&sent,c->timeout_ms);
    if(!e && sent!=12+np*4) e=MTP_IO;
    if(e) goto fail;
    if(outgoing) {
        uint8_t packet[1024];uint32_t ms=remaining(c,start);if(!ms){e=MTP_TIMEOUT;goto fail;}if((op!=0x9804 && op!=0x100c) || outgoing_size>sizeof(packet)-12){e=MTP_STATE;goto fail;}
        put32(packet,(uint32_t)(12+outgoing_size));put16(packet+4,2);put16(packet+6,op);put32(packet+8,c->last_tid);memcpy(packet+12,outgoing,outgoing_size);
        sent=0;e=c->io.write(c->io.user,packet,12+outgoing_size,&sent,ms);if(!e && sent!=12+outgoing_size)e=MTP_IO;if(e)goto fail;
        if(c->out_max_packet && (12+outgoing_size)%c->out_max_packet==0){sent=0;e=c->io.write(c->io.user,packet,0,&sent,remaining(c,start));if(!e && sent)e=MTP_IO;if(e)goto fail;}
    }
    e=container(c,start,0,&type,&code,&p,&size); if(e) goto fail;
    had_data=type==2;
    if(had_data) {
        if(!data) { e=MTP_PROTOCOL; goto fail; }
        e=container(c,start,1,&type,&code,&response,&rn); if(e) goto fail;
        if(type!=3) { e=MTP_PROTOCOL; goto fail; }
    } else if(code==0x2001 && data) { e=MTP_PROTOCOL; goto fail; }
    c->response=code;
    c->response_param_count=(had_data?rn:size)/4;
    for(i=0;i<c->response_param_count;++i) c->response_params[i]=u32((had_data?response:p)+i*4);
    free(response); response=NULL;
    if(c->rx_pos!=c->rx_len) { e=MTP_PROTOCOL; goto fail; }
    if(code!=0x2001) { free(p); return MTP_RESPONSE; }
    *out=p; *n=size; return MTP_OK;
fail:
    free(p); free(response); c->poisoned=1; return e;
}
static int transaction(mtp_client *c,uint16_t op,const uint32_t *params,size_t np,int data,uint8_t **out,size_t *n) {return transaction_ex(c,op,params,np,data,NULL,0,out,n);}
int mtp_supports(const mtp_device_info *v,uint16_t op) {
    size_t i; for(i=0;i<v->operation_count;++i) if(v->operations[i]==op) return 1;
    return 0;
}
int mtp_open_session(mtp_client *c,uint32_t id) {
    uint8_t *p; size_t n; int e;
    if(c->session || !id) return MTP_STATE;
    c->session_recovered=0;
    c->next_tid=0;
    e=transaction(c,0x1002,&id,1,0,&p,&n); free(p);



    if(e==MTP_RESPONSE && c->response==0x201e && !c->poisoned){
        c->session=1;e=mtp_close_session(c);
        if(e && !(e==MTP_RESPONSE && c->response==0x2003 && !c->poisoned))return e;
        c->next_tid=0;e=transaction(c,0x1002,&id,1,0,&p,&n);free(p);if(!e)c->session_recovered=1;
    }
    if(e==MTP_RESPONSE && c->response==0x201e)c->session_cleanup_required=1;
    if(!e) { c->session=1; c->next_tid=1; c->session_cleanup_required=0; }
    return e;
}
int mtp_close_session(mtp_client *c) {
    uint8_t *p; size_t n; int e=MTP_OK;
    if(c->session) {
        e=transaction(c,0x1003,NULL,0,0,&p,&n);free(p);c->session=0;

        if(e==MTP_RESPONSE && c->response==0x2003 && !c->poisoned)e=0;
        c->session_cleanup_required=e!=0;
    }
    return e;
}
#define DATA_CALL(op,params,np,parse) \
    uint8_t *p; size_t n; int e=transaction(c,op,params,np,1,&p,&n); \
    if(!e) e=(parse); free(p); if(e==MTP_PROTOCOL || e==MTP_LIMIT) c->poisoned=1; return e
int mtp_get_device_info(mtp_client *c,mtp_device_info *v) {
    DATA_CALL(0x1001,NULL,0,mtp_parse_device(p,n,v));
}
int mtp_get_storage_ids(mtp_client *c,uint32_t *ids,size_t cap,size_t *count) {
    *count=0; if(cap>MTP_MAX_STORAGES) cap=MTP_MAX_STORAGES;
    DATA_CALL(0x1004,NULL,0,mtp_parse_ids(p,n,ids,cap,count));
}
int mtp_get_storage_info(mtp_client *c,uint32_t id,mtp_storage_info *v) {
    DATA_CALL(0x1005,&id,1,mtp_parse_storage(p,n,v));
}
int mtp_get_handles(mtp_client *c,uint32_t storage,uint32_t parent,uint32_t *ids,size_t cap,size_t *count) {
    uint32_t params[3]={storage,0,parent}; *count=0;
    if(!parent || !storage || storage==UINT32_MAX) return MTP_STATE;
    DATA_CALL(0x1007,params,3,mtp_parse_ids(p,n,ids,cap,count));
}
int mtp_get_object_info(mtp_client *c,uint32_t id,mtp_object_info *v) {
    if(!id || id==UINT32_MAX) return MTP_STATE;
    DATA_CALL(0x1008,&id,1,mtp_parse_object(p,n,v));
}
int mtp_get_object_size(mtp_client *c,uint32_t id,uint64_t *size) {
    uint32_t params[2]={id,0xdc04};
    uint8_t *p; size_t n; int e;
    if(!id || id==MTP_ROOT) return MTP_STATE;
    e=transaction(c,0x9803,params,2,1,&p,&n);
    if(!e) { if(n!=8) { e=MTP_PROTOCOL; c->poisoned=1; } else *size=u64(p); }
    free(p); return e;
}
int mtp_get_partial(mtp_client *c,uint32_t id,uint64_t offset,size_t length,int extended,uint8_t *out,size_t *got) {
    uint32_t params[4]; uint8_t *p; size_t n; int e;
    *got=0;
    if(!id || id==MTP_ROOT || !length || length>MTP_COPY_CHUNK || !out) return MTP_STATE;
    if(!extended && (offset>UINT32_MAX || length>UINT32_MAX-offset)) return MTP_LIMIT;
    if(length>UINT64_MAX-offset)return MTP_LIMIT;
    params[0]=id; params[1]=(uint32_t)offset;
    if(extended) { params[2]=(uint32_t)(offset>>32); params[3]=(uint32_t)length; }
    else params[2]=(uint32_t)length;
    c->content_read=1;
    e=transaction(c,extended?0x95c1:0x101b,params,extended?4:3,1,&p,&n);
    c->content_read=0;
    if(!e) {
        if(n>length || c->response_param_count!=1 || c->response_params[0]!=n) { e=MTP_PROTOCOL; c->poisoned=1; }
        else { if(n) memcpy(out,p,n); *got=n; }
    }
    free(p); return e;
}

int mtp_get_object_stream(mtp_client *c,uint32_t id,uint64_t size,uint8_t *buffer,size_t capacity,mtp_download_consume consume,mtp_download_progress progress,void *user,mtp_download_stats *stats) {
    uint8_t command[16],header[12],*reply=NULL;uint64_t offset=0,start;uint32_t length,wanted;size_t sent=0,n=0,got=0;uint16_t type,code;int rc=0;unsigned zeros=0;
    if(stats)memset(stats,0,sizeof(*stats));
    if(!id || id==MTP_ROOT || !buffer || !consume || capacity<MTP_RX_BYTES || capacity>1024u*1024u || capacity%1024 || !c->session || c->poisoned || !c->io.read || !c->io.write || !c->io.now_ms)return MTP_STATE;
    if(size>UINT64_MAX-12 || c->next_tid==UINT32_MAX)return MTP_LIMIT;
    if(c->rx_pos!=c->rx_len){c->poisoned=1;return MTP_PROTOCOL;}
    if(progress && progress(user,0,size))return MTP_CANCELLED;
    c->last_op=0x1009;c->last_tid=c->next_tid++;c->response=0;c->response_param_count=0;c->content_read=1;
    put32(command,16);put16(command+4,1);put16(command+6,0x1009);put32(command+8,c->last_tid);put32(command+12,id);
    start=c->io.now_ms(c->io.user);rc=c->io.write(c->io.user,command,sizeof(command),&sent,c->timeout_ms);if(!rc && sent!=sizeof(command))rc=MTP_IO;if(rc)goto fail;
    rc=exact_profile(c,header,sizeof(header),start,stats);if(rc)goto fail;
    length=u32(header);type=u16(header+4);code=u16(header+6);
    if(u32(header+8)!=c->last_tid){rc=MTP_PROTOCOL;goto fail;}
    if(type==3){
        if(length<12 || length>32 || (length-12)%4 || (code&0xf000)!=0x2000 || code==0x2001){rc=MTP_PROTOCOL;goto fail;}
        if(length>12){uint8_t parameters[20];rc=exact_profile(c,parameters,length-12,start,stats);if(rc)goto fail;}
        if(c->rx_pos!=c->rx_len){rc=MTP_PROTOCOL;goto fail;}
        c->response=code;c->content_read=0;return MTP_RESPONSE;
    }
    wanted=size+12>=UINT32_MAX?UINT32_MAX:(uint32_t)(size+12);
    if(type!=2 || code!=0x1009 || length!=wanted){rc=MTP_PROTOCOL;goto fail;}
    while(offset<size){
        if(offset && progress && progress(user,offset,size)){rc=MTP_CANCELLED;goto fail;}
        if(c->rx_pos<c->rx_len){
            n=c->rx_len-c->rx_pos;if(n>size-offset)n=(size_t)(size-offset);if(n>capacity)n=capacity;
            memcpy(buffer,c->rx+c->rx_pos,n);c->rx_pos+=n;
        }else{
            uint64_t before,after;c->rx_pos=c->rx_len=0;start=c->io.now_ms(c->io.user);
            do{uint32_t ms=remaining(c,start);if(!ms || zeros>8){rc=MTP_TIMEOUT;goto fail;}
                got=0;before=c->io.now_ms(c->io.user);rc=c->io.read(c->io.user,buffer,capacity,&got,ms);after=c->io.now_ms(c->io.user);
                if(stats){++stats->read_calls;stats->read_bytes+=got;if(after>=before)stats->io_ms+=after-before;}
                if(rc)goto fail;if(got>capacity){rc=MTP_PROTOCOL;goto fail;}if(!remaining(c,start)){rc=MTP_TIMEOUT;goto fail;}
                if(!got)++zeros;
            }while(!got);
            zeros=0;n=got;
            if(n>size-offset){size_t extra=n-(size_t)(size-offset);n=(size_t)(size-offset);if(extra>sizeof(c->rx)){rc=MTP_PROTOCOL;goto fail;}memcpy(c->rx,buffer+n,extra);c->rx_len=extra;}
        }
        rc=consume(user,buffer,n);if(rc)goto fail;offset+=n;
    }
    start=c->io.now_ms(c->io.user);rc=container(c,start,1,&type,&code,&reply,&n);if(rc)goto fail;
    if(type!=3 || n || c->rx_pos!=c->rx_len){rc=MTP_PROTOCOL;goto fail;}
    c->response=code;c->content_read=0;free(reply);
    if(code!=0x2001)return MTP_RESPONSE;
    return progress && progress(user,size,size)?MTP_CANCELLED:0;
fail:
    free(reply);c->poisoned=1;c->content_read=0;return rc;
}


int mtp_delete_object(mtp_client *c,uint32_t id) {
    uint32_t params[2]={id,0};uint8_t *p;size_t n;int e;
    if(!id || id==MTP_ROOT) return MTP_STATE;
    e=transaction(c,0x100b,params,2,0,&p,&n);free(p);return e;
}

#include "psmtp/name.h"
int mtp_rename_object(mtp_client *c,uint32_t id,uint16_t format,const char *name) {
    uint32_t params[2]={0xdc07,format};uint8_t encoded[511],*p;size_t n,length;int rc;
    if(!id || id==MTP_ROOT || psmtp_name_encode(name,encoded,sizeof(encoded),&length))return MTP_STATE;
    rc=transaction(c,0x9802,params,2,1,&p,&n);
    if(!rc && (n<5 || u16(p)!=0xdc07 || u16(p+2)!=0xffff || p[4]!=1))rc=MTP_UNSUPPORTED;free(p);if(rc)return rc;
    params[0]=id;params[1]=0xdc07;
    rc=transaction_ex(c,0x9804,params,2,0,encoded,length,&p,&n);free(p);return rc;
}
int mtp_get_thumbnail(mtp_client *c,uint32_t id,uint8_t **bytes,size_t *size) {
    int rc;*bytes=NULL;*size=0;if(!id || id==MTP_ROOT)return MTP_STATE;
    c->content_read=1;rc=transaction(c,0x100a,&id,1,1,bytes,size);c->content_read=0;return rc;
}




int mtp_send_object_info(mtp_client *c,uint32_t storage,uint32_t parent,const char *name,uint64_t size,int folder,uint32_t *handle) {
    uint8_t data[600],*reply=NULL;size_t encoded,n;uint32_t params[2]={storage,parent};int rc;
    *handle=0;
    if(!storage || storage==MTP_ROOT || !parent || size>MTP_UPLOAD_MAX_SIZE || (folder && size) || psmtp_name_encode(name,data+52,sizeof(data)-55,&encoded))return MTP_LIMIT;
    memset(data,0,52);put32(data,storage);put16(data+4,folder?0x3001:0x3000);put32(data+8,size>=UINT32_MAX?UINT32_MAX:(uint32_t)size);put32(data+38,parent==MTP_ROOT?0:parent);put16(data+42,folder?1:0);
    memset(data+52+encoded,0,3);
    rc=transaction_ex(c,0x100c,params,2,0,data,55+encoded,&reply,&n);free(reply);if(rc)return rc;
    if(c->response_param_count!=3 || c->response_params[0]!=storage ||
       (c->response_params[1]!=parent && !(parent==MTP_ROOT && !c->response_params[1])) || !c->response_params[2] || c->response_params[2]==MTP_ROOT){c->poisoned=1;return MTP_PROTOCOL;}
    *handle=c->response_params[2];return 0;
}
int mtp_send_object(mtp_client *c,uint64_t size,mtp_upload_read source,mtp_upload_progress progress,void *user) {
    uint8_t cmd[12],*packet=NULL,*reply=NULL;uint64_t offset=0,start;size_t sent,n,header=12,chunk;uint16_t type,code;int rc=0;uint32_t saved_timeout=c->timeout_ms;
    if(!source || !c->session || c->poisoned || !c->io.read || !c->io.write || !c->io.now_ms)return MTP_STATE;
    if(size>MTP_UPLOAD_MAX_SIZE || c->next_tid==UINT32_MAX || !c->out_max_packet || c->out_max_packet>1024)return MTP_LIMIT;
    if(c->rx_pos!=c->rx_len){c->poisoned=1;return MTP_PROTOCOL;}
    packet=malloc(MTP_COPY_CHUNK);if(!packet){c->poisoned=1;return MTP_MEMORY;}
    c->last_op=0x100d;c->last_tid=c->next_tid++;c->response=0;c->response_param_count=0;c->content_read=1;
    put32(cmd,12);put16(cmd+4,1);put16(cmd+6,0x100d);put32(cmd+8,c->last_tid);
    start=c->io.now_ms(c->io.user);sent=0;rc=c->io.write(c->io.user,cmd,12,&sent,c->timeout_ms);if(!rc && sent!=12)rc=MTP_IO;if(!rc && !remaining(c,start))rc=MTP_TIMEOUT;if(rc)goto fail;
    put32(packet,size>MTP_UPLOAD_STANDARD_MAX_SIZE?UINT32_MAX:(uint32_t)(size+12));put16(packet+4,2);put16(packet+6,0x100d);put32(packet+8,c->last_tid);
    do{
        if(progress && progress(user,offset,size)){rc=MTP_CANCELLED;goto fail;}
        chunk=(size_t)(size-offset>MTP_COPY_CHUNK-header?MTP_COPY_CHUNK-header:size-offset);

        if(offset+chunk<size)chunk=(chunk+header)/c->out_max_packet*c->out_max_packet-header;
        if(chunk){rc=source(user,packet+header,chunk);if(rc)goto fail;}
        start=c->io.now_ms(c->io.user);sent=0;rc=c->io.write(c->io.user,packet,chunk+header,&sent,c->timeout_ms);
        if(!rc && sent!=chunk+header)rc=MTP_IO;if(!rc && !remaining(c,start))rc=MTP_TIMEOUT;if(rc)goto fail;
        offset+=chunk;header=0;
    }while(offset<size);
    if((size+12)%c->out_max_packet==0){sent=0;start=c->io.now_ms(c->io.user);rc=c->io.write(c->io.user,packet,0,&sent,c->timeout_ms);if(!rc && sent)rc=MTP_IO;if(rc)goto fail;}
    if(progress && progress(user,size,size)){rc=MTP_CANCELLED;goto fail;}


    if(size>MTP_UPLOAD_STANDARD_MAX_SIZE && c->timeout_ms<30000)c->timeout_ms=30000;
    start=c->io.now_ms(c->io.user);rc=container(c,start,1,&type,&code,&reply,&n);if(rc)goto fail;
    if(c->rx_pos!=c->rx_len || type!=3 || n){rc=MTP_PROTOCOL;goto fail;}
    c->response=code;if(code!=0x2001)rc=code==0x200c?MTP_NO_SPACE:MTP_RESPONSE;
    c->timeout_ms=saved_timeout;c->content_read=0;free(reply);free(packet);return rc;
fail:
    c->timeout_ms=saved_timeout;c->poisoned=1;c->content_read=0;free(reply);free(packet);return rc;
}
