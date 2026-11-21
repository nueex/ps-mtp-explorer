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
#include <errno.h>
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
typedef uint64_t copy_crc64_table[8][256];

/* CRC-64/XZ uses the reflected ECMA-182 polynomial. The eight tables
 * combine the contributions of eight successive bytes in one step. */
static void copy_crc64_init(copy_crc64_table table) {
    for(unsigned byte=0;byte<256;++byte) {
        uint64_t crc=byte;
        for(unsigned bit=0;bit<8;++bit)
            crc=(crc>>1)^((crc&1)?UINT64_C(0xc96c5795d7870f42):0);
        table[0][byte]=crc;
    }
    for(unsigned slice=1;slice<8;++slice) {
        for(unsigned byte=0;byte<256;++byte) {
            uint64_t crc=table[slice-1][byte];
            table[slice][byte]=table[0][crc&255]^(crc>>8);
        }
    }
}

static uint64_t copy_crc64_update(copy_crc64_table table,uint64_t crc,const uint8_t *data,size_t n) {
    while(n>=8) {
        crc^=(uint64_t)data[0]|(uint64_t)data[1]<<8|(uint64_t)data[2]<<16|(uint64_t)data[3]<<24|
             (uint64_t)data[4]<<32|(uint64_t)data[5]<<40|(uint64_t)data[6]<<48|(uint64_t)data[7]<<56;
        crc=table[7][crc&255]^table[6][(crc>>8)&255]^table[5][(crc>>16)&255]^table[4][(crc>>24)&255]^
            table[3][(crc>>32)&255]^table[2][(crc>>40)&255]^table[1][(crc>>48)&255]^table[0][crc>>56];
        data+=8;
        n-=8;
    }
    while(n--) crc=table[0][(crc^*data++)&255]^(crc>>8);
    return crc;
}

typedef struct {
    mtp_client *client;const mtp_copy_source *source;const mtp_copy_sink *sink;
    mtp_copy_progress progress;void *user;FILE *out;psmtp_sha256 sha;
    copy_crc64_table *crc_table;
    uint64_t crc;
    uint64_t offset,logged,guarded,phone_ms,write_ms,hash_ms,read_ms,sync_ms,ui_ms,guard_ms,writes,requests;
    uint8_t first[4];size_t first_n;psmtp_verify_capture *capture;int verify_error;
} copy_context;
static uint64_t copy_now(copy_context *x){return x->client->io.now_ms?x->client->io.now_ms(x->client->io.user):0;}
static void copy_elapsed(copy_context *x,uint64_t before,uint64_t *total){uint64_t after=copy_now(x);if(after>=before)*total+=after-before;}
static int copy_report(copy_context *x,int phase){uint64_t before;int cancel;if(!x->progress)return 0;before=copy_now(x);cancel=x->progress(x->user,(enum mtp_copy_phase)phase,x->offset,x->source->size);copy_elapsed(x,before,&x->ui_ms);return cancel;}
static int stream_progress(void *v,uint64_t done,uint64_t total){copy_context *x=v;(void)done;(void)total;return copy_report(x,MTP_COPY_RECEIVE);}
static int copy_consume(void *v,const uint8_t *data,size_t n){
    copy_context *x=v;size_t j,written=0;uint64_t before;int rc;
    if(!n || n>x->source->size-x->offset)return MTP_VERIFY;
    if(x->capture && psmtp_verify_capture_feed(x->capture,x->offset,data,n))return MTP_VERIFY;
    for(j=0;j<n && x->first_n<sizeof(x->first);++j)x->first[x->first_n++]=data[j];
    if(x->source->is_pkg && x->first_n>=4 && memcmp(x->first,"\177CNT",4))return MTP_VERIFY;
    while(written<n){size_t actual=0;before=copy_now(x);rc=x->sink->write(x->sink->user,data+written,n-written,&actual);copy_elapsed(x,before,&x->write_ms);++x->writes;
        if(rc)return rc;if(!actual || actual>n-written)return MTP_IO;written+=actual;
    }
    before=copy_now(x);
    psmtp_sha256_update(&x->sha,data,n);
    if(x->crc_table)x->crc=copy_crc64_update(*x->crc_table,x->crc,data,n);
    copy_elapsed(x,before,&x->hash_ms);x->offset+=n;
    if(x->offset-x->guarded>=UINT64_C(1048576) || x->offset==x->source->size){before=copy_now(x);rc=capacity(x->sink,x->source->size-x->offset);copy_elapsed(x,before,&x->guard_ms);x->guarded=x->offset;if(rc)return rc;}
    if(x->offset-x->logged>=UINT64_C(67108864) || x->offset==x->source->size){if(x->out)fprintf(x->out,"stage=copy-progress phase=1 bytes=%" PRIu64 "/%" PRIu64 "\n",x->offset,x->source->size);x->logged=x->offset;}
    return 0;
}
static int copy_sparse_size(void *user,uint64_t *size){copy_context *x=user;int rc=x->sink->size(x->sink->user,size);if(rc)x->verify_error=rc;return rc?EIO:0;}
static int copy_sparse_read(void *user,uint64_t at,uint8_t *data,size_t n,size_t *got){copy_context *x=user;int rc=x->sink->read_at(x->sink->user,at,data,n,got);if(rc)x->verify_error=rc;return rc?EIO:0;}
static int copy_sparse_cancel(void *user,uint64_t done,uint64_t total){copy_context *x=user;(void)total;x->offset=done;return copy_report(x,MTP_COPY_LOCAL);}
static int copy_run(mtp_client *c,const mtp_copy_source *s,const mtp_copy_sink *sink,mtp_copy_progress progress,void *user,mtp_copy_result *result,FILE *out,enum mtp_copy_verify verify,mtp_copy_stream_reader reader) {
    uint8_t *buf=NULL,hash[32];copy_context x={0};mtp_download_stats stream={0};
    psmtp_verify_capture capture={0};psmtp_verify_workspace local_workspace={0},*workspace=sink?sink->verify_workspace:NULL;
    uint64_t received_crc=0;
    int quick=verify==MTP_COPY_VERIFY_SPARSE;
    uint64_t started_ms=c->io.now_ms?c->io.now_ms(c->io.user):0,receive_ms=0,verify_ms=0,phase_start=0,before;size_t n,k;int e=0,phase=0,started=0,clean,used_stream=s->streamed;
    memset(result,0,sizeof(*result));x.client=c;x.source=s;x.sink=sink;x.progress=progress;x.user=user;x.out=out;
    if(!sink || !sink->begin || !sink->space || !sink->write || !sink->sync || !sink->rewind || !sink->read || !sink->publish || !sink->dispose)return MTP_STATE;
    if(verify!=MTP_COPY_VERIFY_SHA256 && !quick && verify!=MTP_COPY_VERIFY_CRC64)return MTP_STATE;
    if(quick && (!sink->reopen_read || !sink->size || !sink->read_at))return MTP_STATE;
    buf=malloc(MTP_STREAM_CHUNK);if(!buf)return MTP_MEMORY;
    if(quick){if(psmtp_verify_capture_init(&capture,s->size)){e=MTP_MEMORY;goto done;}x.capture=&capture;
        if(!workspace){if(psmtp_verify_workspace_init(&local_workspace)){e=MTP_MEMORY;goto done;}workspace=&local_workspace;}}
    if(verify==MTP_COPY_VERIFY_CRC64) {
        x.crc_table=malloc(sizeof(*x.crc_table));
        if(!x.crc_table){e=MTP_MEMORY;goto done;}
        copy_crc64_init(*x.crc_table);
    }
    e=recheck(c,s);if(e)goto done;started=1;e=sink->begin(sink->user,s);if(e)goto done;
    if(sink->prepare){phase=MTP_COPY_PREPARE;e=sink->prepare(sink->user,progress,user);if(e)goto done;e=recheck(c,s);if(e)goto done;}
    e=capacity(sink,s->size);if(e)goto done;
    if(out)fprintf(out,"stage=copy-path method=%s buffer=%u partial-chunk=%u\n",used_stream?"getobject-stream":"partial",MTP_STREAM_CHUNK,MTP_COPY_CHUNK);
    for(phase=MTP_COPY_RECEIVE;phase<=MTP_COPY_LOCAL;++phase){
        psmtp_sha256_init(&x.sha);x.crc=UINT64_MAX;x.offset=x.logged=x.guarded=0;phase_start=copy_now(&x);
        if(out)fprintf(out,"stage=copy-pass phase=%d total=%" PRIu64 "\n",phase,s->size);
        if(phase==MTP_COPY_LOCAL){before=copy_now(&x);e=sink->sync(sink->user);copy_elapsed(&x,before,&x.sync_ms);if(e)goto done;
            if(quick){psmtp_verify_source original=psmtp_verify_capture_source(&capture),destination={&x,copy_sparse_size,copy_sparse_read};enum psmtp_verify_status status;uint64_t compared=0;
                /* Native sink callbacks close the synced writer and reopen it
                 * read-only, checking the same file identity before sampling. */
                e=sink->reopen_read(sink->user);if(e)goto done;
                status=psmtp_sparse_verify(&original,&destination,workspace,copy_sparse_cancel,&x,&compared);
                if(out)fprintf(out,"stage=copy-sparse-verify status=%d compared=%" PRIu64 " windows=%zu file-bytes=%" PRIu64 " source=original-stream destination=reopened-readonly\n",status,compared,capture.count,s->size);
                if(status!=PSMTP_VERIFY_OK){e=status==PSMTP_VERIFY_CANCELLED?MTP_CANCELLED:status==PSMTP_VERIFY_MISMATCH?MTP_VERIFY:x.verify_error?x.verify_error:MTP_IO;goto done;}
                copy_elapsed(&x,phase_start,&verify_ms);continue;
            }
            e=sink->rewind(sink->user);if(e)goto done;}
        if(phase==MTP_COPY_RECEIVE && used_stream){
            e=reader?reader(c,s->handle,s->size,buf,MTP_STREAM_CHUNK,copy_consume,stream_progress,&x,&stream,out):mtp_get_object_stream(c,s->handle,s->size,buf,MTP_STREAM_CHUNK,copy_consume,stream_progress,&x,&stream);x.phone_ms+=stream.io_ms;



            if(e==MTP_RESPONSE && c->response==0x2005 && !c->poisoned && !x.offset && s->partial_supported && (s->extended || s->size<UINT32_MAX)){
                used_stream=0;e=0;if(out)fputs("stage=copy-path method=partial reason=getobject-not-supported\n",out);
            }
            if(e)goto done;
        }
        if(phase==MTP_COPY_LOCAL || !used_stream){
            for(;;){
                if(copy_report(&x,phase)){e=MTP_CANCELLED;goto done;}if(x.offset==s->size)break;
                n=(size_t)(s->size-x.offset>(phase==MTP_COPY_LOCAL?MTP_STREAM_CHUNK:MTP_COPY_CHUNK)?(phase==MTP_COPY_LOCAL?MTP_STREAM_CHUNK:MTP_COPY_CHUNK):s->size-x.offset);k=0;
                before=copy_now(&x);
                if(phase==MTP_COPY_LOCAL){e=sink->read(sink->user,buf,n,&k);copy_elapsed(&x,before,&x.read_ms);}
                else {e=mtp_get_partial(c,s->handle,x.offset,n,s->extended,buf,&k);copy_elapsed(&x,before,&x.phone_ms);++x.requests;}
                if(e)goto done;if(!k || k>n){e=MTP_VERIFY;goto done;}
                if(phase==MTP_COPY_RECEIVE){e=copy_consume(&x,buf,k);if(e)goto done;}
                else {before=copy_now(&x);
                    if(x.crc_table)x.crc=copy_crc64_update(*x.crc_table,x.crc,buf,k);
                    else psmtp_sha256_update(&x.sha,buf,k);
                    copy_elapsed(&x,before,&x.hash_ms);x.offset+=k;
                    if(x.offset-x.logged>=UINT64_C(67108864) || x.offset==s->size){if(out)fprintf(out,"stage=copy-progress phase=2 bytes=%" PRIu64 "/%" PRIu64 "\n",x.offset,s->size);x.logged=x.offset;}}
            }
        }
        if(phase==MTP_COPY_RECEIVE && s->is_pkg && x.first_n<4){e=MTP_VERIFY;goto done;}
        if(phase==MTP_COPY_LOCAL && !quick){k=0;e=sink->read(sink->user,buf,1,&k);if(e)goto done;if(k){e=MTP_VERIFY;goto done;}}
        if(phase==MTP_COPY_RECEIVE){
            psmtp_sha256_final(&x.sha,result->digest);
            received_crc=x.crc;
            copy_elapsed(&x,phase_start,&receive_ms);
        }else{
            int mismatch;
            if(x.crc_table)mismatch=x.crc!=received_crc;
            else{
                psmtp_sha256_final(&x.sha,hash);
                mismatch=memcmp(hash,result->digest,sizeof(hash))!=0;
            }
            copy_elapsed(&x,phase_start,&verify_ms);
            if(mismatch){
                if(out)fprintf(out,"stage=copy-readback-mismatch checksum=%s bytes=%" PRIu64 "\n",x.crc_table?"crc64":"sha256",x.offset);
                e=MTP_VERIFY;goto done;
            }
        }
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
        if(!e && result->published){fputs("copy-sha256=",out);for(n=0;n<32;++n)fprintf(out,"%02x",result->digest[n]);fprintf(out," local-reread=%s phone-content-reads=1 verification=%s installation=not-started\n",quick?"sparse-match":"match",quick?"sparse-head-body-tail-size":x.crc_table?"full-crc64-reread":"full-sha256-reread");}
    }
    psmtp_verify_capture_free(&capture);psmtp_verify_workspace_free(&local_workspace);free(x.crc_table);free(buf);return e;
}

int mtp_copy_run(mtp_client *c,const mtp_copy_source *s,const mtp_copy_sink *sink,mtp_copy_progress progress,void *user,mtp_copy_result *result,FILE *out){return copy_run(c,s,sink,progress,user,result,out,MTP_COPY_VERIFY_SHA256,NULL);}
int mtp_copy_run_quick(mtp_client *c,const mtp_copy_source *s,const mtp_copy_sink *sink,mtp_copy_progress progress,void *user,mtp_copy_result *result,FILE *out){return copy_run(c,s,sink,progress,user,result,out,MTP_COPY_VERIFY_SPARSE,NULL);}
int mtp_copy_run_streamed(mtp_client *c,const mtp_copy_source *s,const mtp_copy_sink *sink,mtp_copy_progress progress,void *user,mtp_copy_result *result,FILE *out,enum mtp_copy_verify verify,mtp_copy_stream_reader reader){return copy_run(c,s,sink,progress,user,result,out,verify,reader);}
