/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#include "psmtp/copy.h"
#include "psmtp/sha256.h"
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <ctype.h>
static int same(const mtp_object_info *a,const mtp_object_info *b) {
    return a->storage==b->storage && a->parent==b->parent && a->format==b->format && a->size32==b->size32 &&
           !strcmp(a->name,b->name) && !strcmp(a->modified,b->modified);
}
static int pkg(const char *s) {
    size_t n=strlen(s); return n>=4 && s[n-4]=='.' && tolower((unsigned char)s[n-3])=='p' &&
        tolower((unsigned char)s[n-2])=='k' && tolower((unsigned char)s[n-1])=='g';
}
int mtp_copy_probe(mtp_client *c,uint32_t id,const mtp_object_info *selected,mtp_copy_source *s,FILE *out) {
    mtp_device_info *dev; int e; uint64_t size;
    memset(s,0,sizeof(*s)); s->handle=id;
    e=mtp_get_object_info(c,id,&s->info); if(e) return e;
    if(s->info.format==0x3001 || !same(selected,&s->info)) return MTP_VERIFY;
    dev=(mtp_device_info *)malloc(sizeof(*dev)); if(!dev) return MTP_MEMORY;
    e=mtp_get_device_info(c,dev);
    if(!e) {
        s->size_property=mtp_supports(dev,0x9803);
        s->extended=mtp_supports(dev,0x95c1);
        s->streamed=mtp_supports(dev,0x1009);s->partial_supported=s->extended || mtp_supports(dev,0x101b);
        if(!s->streamed && !s->partial_supported) e=MTP_UNSUPPORTED;
        if(mtp_supports(dev,0x101b) && s->info.size32!=UINT32_MAX) s->extended=0;
    }
    free(dev); if(e) return e;
    size=s->info.size32;
    if(s->size_property) {
        e=mtp_get_object_size(c,id,&size);


        if(e==MTP_RESPONSE && s->info.size32!=UINT32_MAX) { s->size_property=0; size=s->info.size32; e=0; }
        if(e) return e;
    } else if(s->info.size32==UINT32_MAX) return MTP_UNSUPPORTED;
    if(s->info.size32!=UINT32_MAX && size!=s->info.size32) return MTP_VERIFY;
    if(size>=UINT32_MAX && !s->extended && !s->streamed) return MTP_UNSUPPORTED;
    if(size>UINT64_MAX/8) return MTP_LIMIT;
    s->size=size; s->is_pkg=pkg(s->info.name);
    if(out) fprintf(out,"stage=copy-probe handle=%08" PRIx32 " bytes=%" PRIu64 " partial64=%d size64=%d stream=%d pkg=%d names-in-log=0\n",
        id,size,s->extended,s->size_property,s->streamed,s->is_pkg);
    return 0;
}
static int recheck(mtp_client *c,const mtp_copy_source *s) {
    mtp_object_info info; uint64_t size; int e=mtp_get_object_info(c,s->handle,&info);
    if(e) return e;
    if(!same(&info,&s->info)) return MTP_VERIFY;
    if(s->size_property) { e=mtp_get_object_size(c,s->handle,&size); if(e) return e; if(size!=s->size) return MTP_VERIFY; }
    return 0;
}
static int capacity(const mtp_copy_sink *sink,uint64_t remaining) {
    uint64_t available=0; int e=sink->space(sink->user,&available);
    if(e==MTP_COPY_SPACE_STREAMING) return 0;
    if(e) return e;
    return available<remaining?MTP_LIMIT:0;
}
typedef struct {
    mtp_client *client;const mtp_copy_source *source;const mtp_copy_sink *sink;
    mtp_copy_progress progress;void *user;FILE *out;psmtp_sha256 sha;
    uint64_t offset,logged,guarded,phone_ms,write_ms,hash_ms,read_ms,sync_ms,ui_ms,guard_ms,writes,requests;
    uint8_t first[4096];size_t first_n;
} copy_context;
static uint64_t copy_now(copy_context *x){return x->client->io.now_ms?x->client->io.now_ms(x->client->io.user):0;}
static void copy_elapsed(copy_context *x,uint64_t before,uint64_t *total){uint64_t after=copy_now(x);if(after>=before)*total+=after-before;}
static int copy_report(copy_context *x,int phase){uint64_t before;int cancel;if(!x->progress)return 0;before=copy_now(x);cancel=x->progress(x->user,(enum mtp_copy_phase)phase,x->offset,x->source->size);copy_elapsed(x,before,&x->ui_ms);return cancel;}
static int stream_progress(void *v,uint64_t done,uint64_t total){copy_context *x=v;(void)done;(void)total;return copy_report(x,MTP_COPY_RECEIVE);}
static int copy_consume(void *v,const uint8_t *data,size_t n){
    copy_context *x=v;size_t j,written=0;uint64_t before;int rc;
    if(!n || n>x->source->size-x->offset)return MTP_VERIFY;
    for(j=0;j<n && x->first_n<sizeof(x->first);++j)x->first[x->first_n++]=data[j];
    if(x->source->is_pkg && x->first_n>=4 && memcmp(x->first,"\177CNT",4))return MTP_VERIFY;
    while(written<n){size_t actual=0;before=copy_now(x);rc=x->sink->write(x->sink->user,data+written,n-written,&actual);copy_elapsed(x,before,&x->write_ms);++x->writes;
        if(rc)return rc;if(!actual || actual>n-written)return MTP_IO;written+=actual;
    }
    before=copy_now(x);psmtp_sha256_update(&x->sha,data,n);copy_elapsed(x,before,&x->hash_ms);x->offset+=n;
    if(x->offset-x->guarded>=UINT64_C(1048576) || x->offset==x->source->size){before=copy_now(x);rc=capacity(x->sink,x->source->size-x->offset);copy_elapsed(x,before,&x->guard_ms);x->guarded=x->offset;if(rc)return rc;}
    if(x->offset-x->logged>=UINT64_C(67108864) || x->offset==x->source->size){if(x->out)fprintf(x->out,"stage=copy-progress phase=1 bytes=%" PRIu64 "/%" PRIu64 "\n",x->offset,x->source->size);x->logged=x->offset;}
    return 0;
}
static int copy_run(mtp_client *c,const mtp_copy_source *s,const mtp_copy_sink *sink,mtp_copy_progress progress,void *user,mtp_copy_result *result,FILE *out,int quick) {
    uint8_t *buf=NULL,hash[32];copy_context x={0};mtp_download_stats stream={0};
    uint64_t started_ms=c->io.now_ms?c->io.now_ms(c->io.user):0,receive_ms=0,verify_ms=0,phase_start=0,before;size_t n,k;int e=0,phase=0,started=0,clean,used_stream=s->streamed;
    memset(result,0,sizeof(*result));x.client=c;x.source=s;x.sink=sink;x.progress=progress;x.user=user;x.out=out;
    if(!sink || !sink->begin || !sink->space || !sink->write || !sink->sync || !sink->rewind || !sink->read || !sink->publish || !sink->dispose)return MTP_STATE;
    buf=malloc(MTP_STREAM_CHUNK);if(!buf)return MTP_MEMORY;
    e=recheck(c,s);if(e)goto done;started=1;e=sink->begin(sink->user,s);if(e)goto done;
    if(sink->prepare){phase=MTP_COPY_PREPARE;e=sink->prepare(sink->user,progress,user);if(e)goto done;e=recheck(c,s);if(e)goto done;}
    e=capacity(sink,s->size);if(e)goto done;
    if(out)fprintf(out,"stage=copy-path method=%s buffer=%u partial-chunk=%u\n",used_stream?"getobject-stream":"partial",MTP_STREAM_CHUNK,MTP_COPY_CHUNK);
    for(phase=MTP_COPY_RECEIVE;phase<=MTP_COPY_LOCAL;++phase){
        psmtp_sha256_init(&x.sha);x.offset=x.logged=x.guarded=0;phase_start=copy_now(&x);
        if(out)fprintf(out,"stage=copy-pass phase=%d total=%" PRIu64 "\n",phase,s->size);
        if(phase==MTP_COPY_LOCAL){before=copy_now(&x);e=sink->sync(sink->user);copy_elapsed(&x,before,&x.sync_ms);if(e)goto done;e=sink->rewind(sink->user);if(e)goto done;}
        if(phase==MTP_COPY_RECEIVE && used_stream){
            e=mtp_get_object_stream(c,s->handle,s->size,buf,MTP_STREAM_CHUNK,copy_consume,stream_progress,&x,&stream);x.phone_ms+=stream.io_ms;



            if(e==MTP_RESPONSE && c->response==0x2005 && !c->poisoned && !x.offset && s->partial_supported && (s->extended || s->size<UINT32_MAX)){
                used_stream=0;e=0;if(out)fputs("stage=copy-path method=partial reason=getobject-not-supported\n",out);
            }
            if(e)goto done;
        }
        if(phase==MTP_COPY_LOCAL || !used_stream){
            for(;;){
                if(copy_report(&x,phase)){e=MTP_CANCELLED;goto done;}if(x.offset==(phase==MTP_COPY_LOCAL && quick?x.first_n:s->size))break;
                n=(size_t)(s->size-x.offset>(phase==MTP_COPY_LOCAL?MTP_STREAM_CHUNK:MTP_COPY_CHUNK)?(phase==MTP_COPY_LOCAL?MTP_STREAM_CHUNK:MTP_COPY_CHUNK):s->size-x.offset);k=0;
                if(phase==MTP_COPY_LOCAL && quick)n=x.first_n-(size_t)x.offset;
                before=copy_now(&x);
                if(phase==MTP_COPY_LOCAL){e=sink->read(sink->user,buf,n,&k);copy_elapsed(&x,before,&x.read_ms);}
                else {e=mtp_get_partial(c,s->handle,x.offset,n,s->extended,buf,&k);copy_elapsed(&x,before,&x.phone_ms);++x.requests;}
                if(e)goto done;if(!k || k>n){e=MTP_VERIFY;goto done;}
                if(phase==MTP_COPY_RECEIVE){e=copy_consume(&x,buf,k);if(e)goto done;}
                else {if(quick && memcmp(buf,x.first+(size_t)x.offset,k)){e=MTP_VERIFY;goto done;}before=copy_now(&x);psmtp_sha256_update(&x.sha,buf,k);copy_elapsed(&x,before,&x.hash_ms);x.offset+=k;
                    if(x.offset-x.logged>=UINT64_C(67108864) || x.offset==s->size){if(out)fprintf(out,"stage=copy-progress phase=2 bytes=%" PRIu64 "/%" PRIu64 "\n",x.offset,s->size);x.logged=x.offset;}}
            }
        }
        if(phase==MTP_COPY_RECEIVE && s->is_pkg && x.first_n<4){e=MTP_VERIFY;goto done;}
        if(phase==MTP_COPY_LOCAL && !quick){k=0;e=sink->read(sink->user,buf,1,&k);if(e)goto done;if(k){e=MTP_VERIFY;goto done;}}
        psmtp_sha256_final(&x.sha,hash);
        if(phase==MTP_COPY_RECEIVE){memcpy(result->digest,hash,32);copy_elapsed(&x,phase_start,&receive_ms);}
        else {copy_elapsed(&x,phase_start,&verify_ms);if(!quick && memcmp(hash,result->digest,32)){e=MTP_VERIFY;goto done;}}
    }
    e=recheck(c,s);if(e)goto done;if(progress && progress(user,MTP_COPY_PUBLISH,s->size,s->size)){e=MTP_CANCELLED;goto done;}
    before=copy_now(&x);e=sink->sync(sink->user);copy_elapsed(&x,before,&x.sync_ms);if(e)goto done;e=sink->publish(sink->user,&result->published);
done:
    if(e && phase==MTP_COPY_RECEIVE)copy_elapsed(&x,phase_start,&receive_ms);
    if(e && phase==MTP_COPY_LOCAL && !verify_ms)copy_elapsed(&x,phase_start,&verify_ms);
    if(started){clean=sink->dispose(sink->user);if(clean){result->cleanup_failed=1;if(!e)e=clean;}}
    if(out){
        fprintf(out,"stage=copy-end status=%d phase=%d offset=%" PRIu64 " published=%d cleanup-failed=%d op=%04x response=%04x poisoned=%d\n",e,buf?phase:0,x.offset,result->published,result->cleanup_failed,c->last_op,c->response,c->poisoned);
        if(c->io.now_ms){uint64_t ended=copy_now(&x);fprintf(out,"stage=copy-timing bytes=%" PRIu64 " elapsed-ms=%" PRIu64 " receive-ms=%" PRIu64 " verify-ms=%" PRIu64 " status=%d\n",s->size,ended>=started_ms?ended-started_ms:0,receive_ms,verify_ms,e);}
        fprintf(out,"stage=copy-profile method=%s partial-requests=%" PRIu64 " usb-read-calls=%" PRIu64 " phone-ms=%" PRIu64 " writes=%" PRIu64 " write-ms=%" PRIu64 " hash-ms=%" PRIu64 " reread-ms=%" PRIu64 " sync-ms=%" PRIu64 " ui-ms=%" PRIu64 " guard-ms=%" PRIu64 "\n",used_stream?"getobject-stream":"partial",x.requests,stream.read_calls,x.phone_ms,x.writes,x.write_ms,x.hash_ms,x.read_ms,x.sync_ms,x.ui_ms,x.guard_ms);
        if(!e && result->published){fputs("copy-sha256=",out);for(n=0;n<32;++n)fprintf(out,"%02x",result->digest[n]);fprintf(out," local-reread=%s phone-content-reads=1 verification=%s installation=not-started\n",quick?"quick-prefix":"match",quick?"quick-prefix-size":"full-sha256-reread");}
    }
    free(buf);return e;
}

int mtp_copy_run(mtp_client *c,const mtp_copy_source *s,const mtp_copy_sink *sink,mtp_copy_progress progress,void *user,mtp_copy_result *result,FILE *out){return copy_run(c,s,sink,progress,user,result,out,0);}
int mtp_copy_run_quick(mtp_client *c,const mtp_copy_source *s,const mtp_copy_sink *sink,mtp_copy_progress progress,void *user,mtp_copy_result *result,FILE *out){return copy_run(c,s,sink,progress,user,result,out,1);}
