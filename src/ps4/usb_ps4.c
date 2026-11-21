/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#include "lifecycle.h"
/* Experimental adapter. Three SDK declarations are corrected against the
 * user's actual library: see docs/USB_ABI_1100.md for hash and disassembly.
 * Other USB behavior still awaits device tests. */
#ifndef PSMTP_EXPERIMENTAL_PS4_USB
#error Explicitly enable PSMTP_EXPERIMENTAL_PS4_USB after reading the source review
#endif
#ifdef PSMTP_TEST_USB_RUNTIME
#include "ps4_usb_test_api.h"
#else
#include <orbis/libkernel.h>
#include <orbis/Usbd.h>
#include <orbis/Sysmodule.h>
#endif
#include "psmtp/usb_module.h"
#include "psmtp/browser.h"
#include "psmtp/diagnostic.h"
#include "psmtp/device_watch.h"
#include "psmtp/usb_label.h"
#include "psmtp/usb_select.h"
#include "android_accessory.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "monotonic.h"
#include <errno.h>
#include <limits.h>

typedef char psmtp_usb_device_layout[(sizeof(struct libusb_device_descriptor)==18 &&
    offsetof(struct libusb_device_descriptor,idVendor)==8)?1:-1];

typedef struct {
    int32_t (*init)(void);
    void (*exit)(void);
    int64_t (*list)(libusb_device ***);
    void (*free_list)(libusb_device **,int32_t);
    int32_t (*descriptor)(libusb_device *,struct libusb_device_descriptor *);
    uint8_t (*bus)(libusb_device *);
    uint8_t (*address)(libusb_device *);
    int32_t (*open)(libusb_device *,libusb_device_handle **);
    void (*close)(libusb_device_handle *);
    int32_t (*configuration)(libusb_device_handle *,int32_t *);
    int32_t (*claim)(libusb_device_handle *,int32_t);
    int32_t (*release)(libusb_device_handle *,int32_t);
    int32_t (*control)(libusb_device_handle *,uint8_t,uint8_t,uint16_t,uint16_t,unsigned char *,uint16_t,uint32_t);
    int32_t (*bulk)(libusb_device_handle *,unsigned char,unsigned char *,int32_t,int32_t *,uint32_t);
} usbd_api;
typedef struct { usbd_api *api; libusb_device_handle *handle; usb_mtp_interface selected; FILE *log; mtp_client *client;libusb_device **devices;int claimed,restore_alt,configuration;uint8_t previous_alt,bus,address;uint16_t vendor,product; } ps4_io;
#include "psmtp/cleanup_log.h"

static int usb_stopping;
static void finish_mtp(ps4_io *,mtp_client *,FILE *);
static int close_connection(ps4_io *,FILE *);

typedef char psmtp_module_info_layout[(sizeof(OrbisKernelModuleInfo)==0x160 &&
    offsetof(OrbisKernelModuleInfo,name)==8 && sizeof(OrbisKernelModule)==4)?1:-1];

static int32_t usbd_is_loaded(void *unused) { (void)unused; return sceSysmoduleIsLoaded(ORBIS_SYSMODULE_USBD); }
static int32_t usbd_load(void *unused) { (void)unused; return sceSysmoduleLoadModule(ORBIS_SYSMODULE_USBD); }
static int32_t usbd_unload(void *unused) { (void)unused; return sceSysmoduleUnloadModule(ORBIS_SYSMODULE_USBD); }
static int32_t usbd_find_handle(void *user,int32_t *handle) {
    OrbisKernelModule handles[128]; size_t count=0,i; int32_t rc,found=-1;
    FILE *out=(FILE *)user;
    for(i=0;i<128;++i) handles[i]=UINT32_MAX;
    /* flatz uses entry count; a GoldHEN example passes bytes. Use a numeric
     * size that fits this buffer under either interpretation, then reject an
     * unfilled entry. Do not silently assume the units are verified on 11.00. */
    fputs("stage=usbd-module-list begin size-arg=128 buffer-entries=128 units=ABI-unverified\n",out); fflush(out);
    rc=sceKernelGetModuleList(handles,128,&count);
    fprintf(out,"stage=usbd-module-list rc=0x%08" PRIx32 " count=%zu\n",(uint32_t)rc,count);
    if(rc) return rc;
    if(count>128) { fputs("stage=usbd-module-list-check local-limit-exceeded=1\n",out); return -1; }
    for(i=0;i<count;++i) {
        OrbisKernelModuleInfo info; const char *name; size_t n;
        if(handles[i]==UINT32_MAX) { fputs("stage=usbd-module-list-check unfilled-handle=1\n",out); return -1; }
        memset(&info,0,sizeof(info)); info.size=sizeof(info);
        rc=sceKernelGetModuleInfo(handles[i],&info);
        fprintf(out,"stage=usbd-module-info index=%zu rc=0x%08" PRIx32 "\n",i,(uint32_t)rc);
        if(rc) return rc;
        n=strnlen(info.name,sizeof(info.name));
        if(n==sizeof(info.name)) { fputs("stage=usbd-module-name-check unterminated=1\n",out); return -1; }
        name=strrchr(info.name,'/'); name=name?name+1:info.name;
        if(!strcmp(name,"libSceUsbd") || !strcmp(name,"libSceUsbd.sprx") || !strcmp(name,"libSceUsbd.prx")) {
            if(found>=0 || handles[i]>INT32_MAX) { fputs("stage=usbd-module-match ambiguous-or-invalid=1\n",out); return -1; }
            found=(int32_t)handles[i];
        }
    }
    if(found<0) { fputs("stage=usbd-module-match absent=1; export resolution SKIPPED\n",out); return -1; }
    *handle=found;
    fprintf(out,"stage=usbd-module-match handle=%" PRId32 " name=libSceUsbd\n",found);
    return 0;
}

static int resolve(int32_t module,void *target,size_t size,const char *name,FILE *log) {
    void *address=NULL; int32_t rc=sceKernelDlsym(module,name,&address);
    fprintf(log,"stage=resolve symbol=%s rc=0x%08" PRIx32 " present=%d\n",name,(uint32_t)rc,address!=NULL);
    if(rc || !address || size!=sizeof(address)) return -1;
    memcpy(target,&address,size); return 0;
}
#define RESOLVE(field,symbol) do { if(resolve(module,&api.field,sizeof(api.field),symbol,out)) goto cleanup; } while(0)
static uint64_t now_ms(void *unused) {
    struct timespec ts; (void)unused;
    if(psmtp_monotonic_time(&ts)) return UINT64_MAX;
    return (uint64_t)ts.tv_sec*1000+(uint64_t)ts.tv_nsec/1000000;
}
/* Only the original USB producer calls this gate after an in-flight bulk call
 * has returned. Never open a replacement handle or replay an MTP transaction. */
static int resume_connection(void *user){
    ps4_io *io=user;libusb_device **fresh=NULL;int64_t count;size_t i;int matches=0;int32_t config=-1,rc;
    if(!io || !io->handle || !io->claimed || usb_stopping)return MTP_DISCONNECTED;
    count=io->api->list(&fresh);
    if(count<0 || count>128 || (count && !fresh)){if(count>=0 && fresh)io->api->free_list(fresh,1);errno=ENODEV;return MTP_DISCONNECTED;}
    for(i=0;i<(size_t)count;++i){struct libusb_device_descriptor d;
        if(!fresh[i]){matches=-1;break;}
        if(io->api->bus(fresh[i])!=io->bus || io->api->address(fresh[i])!=io->address)continue;
        memset(&d,0,sizeof(d));if(io->api->descriptor(fresh[i],&d)){matches=-1;break;}
        if(d.idVendor==io->vendor && d.idProduct==io->product)++matches;
    }
    if(fresh)io->api->free_list(fresh,1);
    if(matches!=1){errno=ENODEV;return MTP_DISCONNECTED;}
    rc=io->api->configuration(io->handle,&config);
    if(rc || config!=io->configuration){if(io->client && rc)io->client->background_native_error=rc;errno=ENXIO;return MTP_DISCONNECTED;}
    if(io->selected.alternate){uint8_t alt=0;rc=io->api->control(io->handle,0x81,10,0,io->selected.interface_number,&alt,1,250);
        if(rc!=1 || alt!=io->selected.alternate){errno=EIO;return MTP_DISCONNECTED;}}
    return 0;
}
static int bulk(ps4_io *io,uint8_t ep,uint8_t *buf,size_t n,size_t *actual,uint32_t timeout) {
    int32_t transferred=0,rc;int quiet=io->client && io->client->background_io;
    if((usb_stopping || psmtp_lifecycle_requested()) &&
       (quiet || !io->client || io->client->last_op!=0x1003)){*actual=0;return MTP_CANCELLED;}
    if(!quiet && (!io->client || !io->client->content_read)){
    fprintf(io->log,"stage=bulk begin endpoint=0x%02x requested=%zu timeout-ms=%" PRIu32 "\n",ep,n,timeout);
    fflush(io->log);}
    rc=io->api->bulk(io->handle,ep,buf,(int32_t)n,&transferred,timeout);
    if(quiet && rc)io->client->background_native_error=rc;
    if(!quiet && (rc || transferred<0 || (size_t)transferred>n || !io->client || !io->client->content_read)) fprintf(io->log,"stage=bulk endpoint=0x%02x requested=%zu actual=%" PRId32 " timeout-ms=%" PRIu32 " rc=0x%08" PRIx32 "\n",
            ep,n,transferred,timeout,(uint32_t)rc);
    *actual=0;
    if(transferred<0 || (size_t)transferred>n) return MTP_PROTOCOL;
    *actual=(size_t)transferred;
    /* Do not guess Sony's error namespace from libusb constants. All nonzero
     * returns stop the stream; the unchanged rc above is the hardware evidence.
     * The protocol's own monotonic deadline still reports MTP_TIMEOUT. */
    return rc?MTP_IO:MTP_OK;
}
static int read_bulk(void *v,uint8_t *p,size_t n,size_t *got,uint32_t ms) {
    ps4_io *io=(ps4_io *)v; return bulk(io,io->selected.bulk_in,p,n,got,ms);
}
static int write_bulk(void *v,const uint8_t *p,size_t n,size_t *got,uint32_t ms) {
    ps4_io *io=(ps4_io *)v;
    /* Current USB declaration takes mutable buffer even for OUT. */
    return bulk(io,io->selected.bulk_out,(uint8_t *)(uintptr_t)p,n,got,ms);
}
static void identify_vendor_mtp(usbd_api *api,libusb_device_handle *h,usb_alt *alts,size_t count,FILE *out,uint32_t timeout){
    uint8_t bytes[USB_MAX_CONFIG_BYTES],interfaces[256]={0},code=0;size_t i;int possible=0;int32_t rc;uint16_t lang=0x0409;
    for(i=0;i<count;++i)if(alts[i].cls==0xff){usb_alt copy=alts[i];usb_mtp_interface candidate;copy.mtp_identified=1;
        if(!usb_select_mtp(&copy,1,&candidate))possible=1;
    }
    if(!possible)return;
    rc=api->control(h,0x80,6,0x03ee,0,bytes,18,timeout);
    int valid=rc==18 && !usb_mtp_os_vendor_code(bytes,18,&code);
    fprintf(out,"stage=mtp-os-string native=%" PRId32 " valid=%d\n",rc,valid);
    if(valid){
        rc=api->control(h,0xc0,code,0,4,bytes,16,timeout);
        fprintf(out,"stage=mtp-compatible-header native=%" PRId32 "\n",rc);
        if(rc==16){uint32_t n=(uint32_t)bytes[0]|(uint32_t)bytes[1]<<8|(uint32_t)bytes[2]<<16|(uint32_t)bytes[3]<<24;
            if(n>=16 && n<=sizeof(bytes) && n==16+(uint32_t)bytes[8]*24 && bytes[4]==0 && bytes[5]==1 && bytes[6]==4 && !bytes[7]){
                rc=api->control(h,0xc0,code,0,4,bytes,(uint16_t)n,timeout);
                int parsed=rc==(int32_t)n?usb_mtp_compatible_interfaces(bytes,n,interfaces):-3;
                fprintf(out,"stage=mtp-compatible-body requested=%" PRIu32 " native=%" PRId32 " parsed=%d\n",n,rc,parsed);
            }else fputs("stage=mtp-compatible-header invalid=1\n",out);
        }
    }
    for(i=0;i<count;++i)if(alts[i].cls==0xff && interfaces[alts[i].number]){
        alts[i].mtp_identified=1;fprintf(out,"stage=mtp-interface-evidence interface=%u alt=%u source=os-compatible-id\n",alts[i].number,alts[i].alternate);
    }
    /* Positive descriptor evidence only: never guess MTP from VID or a bulk pair. */
    int language_read=0;
    for(i=0;i<count;++i)if(alts[i].cls==0xff && !alts[i].mtp_identified && alts[i].string_index){
        usb_alt copy=alts[i];usb_mtp_interface candidate;char label[96];copy.mtp_identified=1;
        if(usb_select_mtp(&copy,1,&candidate))continue;
        if(!language_read){
            rc=api->control(h,0x80,6,0x300,0,bytes,255,timeout);language_read=1;
            if(rc>=4 && rc<=255 && bytes[1]==3 && bytes[0]>=4 && bytes[0]<=rc && !(bytes[0]&1))lang=(uint16_t)(bytes[2]|(uint16_t)bytes[3]<<8);
        }
        rc=api->control(h,0x80,6,(uint16_t)(0x300|alts[i].string_index),lang,bytes,255,timeout);
        int matched=rc>=2 && rc<=255 && !psmtp_usb_label(bytes,(size_t)rc,label,sizeof(label)) && !strcmp(label,"MTP");
        fprintf(out,"stage=mtp-interface-string interface=%u alt=%u native=%" PRId32 " mtp=%d contents-logged=0\n",alts[i].number,alts[i].alternate,rc,matched);
        if(matched)alts[i].mtp_identified=1;
    }
}
static int inspect(usbd_api *api,libusb_device_handle *h,unsigned configs,usb_mtp_interface *chosen,FILE *out,uint32_t timeout) {
    uint8_t bytes[USB_MAX_CONFIG_BYTES];usb_alt alts[USB_MAX_ALTS];int32_t active=0,rc;unsigned idx;
    rc=api->configuration(h,&active);
    fprintf(out,"stage=get-configuration native=%" PRId32 " active=%" PRId32 "\n",rc,active);
    if(rc || active<=0 || active>255 || !configs || configs>8){fputs("stage=usb-discovery-failed reason=configuration-unavailable configuration-not-changed=1\n",out);return -1;}
    for(idx=0;idx<configs;++idx){size_t n,i,j,count=0;uint8_t value=0;usb_mtp_interface candidate;
        rc=api->control(h,0x80,6,(uint16_t)(0x200|idx),0,bytes,9,timeout);
        fprintf(out,"stage=config-header index=%u native=%" PRId32 "\n",idx,rc);
        if(rc!=9 || bytes[0]!=9 || bytes[1]!=2){fputs("stage=config-header-check invalid=1 continuing-to-find-active=1\n",out);continue;}
        if(bytes[5]!=active){fprintf(out,"stage=config-skip index=%u value=%u reason=inactive\n",idx,bytes[5]);continue;}
        n=(size_t)bytes[2]|(size_t)bytes[3]<<8;
        if(n<9 || n>sizeof(bytes)){fprintf(out,"stage=config-length-check invalid=1 length=%zu\n",n);return -1;}
        rc=api->control(h,0x80,6,(uint16_t)(0x200|idx),0,bytes,(uint16_t)n,timeout);
        fprintf(out,"stage=config-body index=%u requested=%zu native=%" PRId32 "\n",idx,n,rc);
        if(rc!=(int32_t)n)return -1;
        rc=usb_parse_config(bytes,n,alts,&count,&value);fprintf(out,"stage=config-parse index=%u status=%d\n",idx,rc);if(rc)return -1;
        if(value!=active){fputs("stage=usb-discovery-failed reason=configuration-changed-during-read\n",out);return -1;}
        for(i=0;i<count;++i){fprintf(out,"config=%u interface=%u alt=%u class=%02x/%02x/%02x endpoints=%zu\n",value,alts[i].number,alts[i].alternate,alts[i].cls,alts[i].subclass,alts[i].protocol,alts[i].endpoint_count);
            for(j=0;j<alts[i].endpoint_count;++j){usb_endpoint *ep=&alts[i].endpoints[j];fprintf(out," endpoint=%02x attributes=%02x max-packet=%u\n",ep->address,ep->attributes,ep->max_packet);}}
        rc=usb_select_mtp(alts,count,&candidate);
        if(rc==-1){identify_vendor_mtp(api,h,alts,count,out,timeout);rc=usb_select_mtp(alts,count,&candidate);}
        fprintf(out,"stage=select-interface config=%u active=1 result=%d\n",value,rc);
        if(!rc){*chosen=candidate;fprintf(out,"stage=mtp-candidate interface=%u alt=%u\n",candidate.interface_number,candidate.alternate);return 0;}
        fprintf(out,"stage=usb-discovery-failed reason=%s mtp-traffic-sent=0\n",rc==-2?"ambiguous-interfaces":"no-identified-mtp-interface");return -2;
    }
    fputs("stage=usb-discovery-failed reason=active-descriptor-unavailable\n",out);return -1;
}

/* The context stays initialized for this launch. Discovery and all MTP I/O
 * remain serialized; preview jobs exclusively borrow the client while the
 * main thread suspends device polling and other MTP operations. */
static struct {usbd_api api;psmtp_usb_module module;psmtp_device_watch watch;libusb_device **snapshot;uint64_t retry_at;int ready;ps4_io *active;} runtime;
static int usb_ready(FILE *out) {
    usbd_api api={0};int32_t module,rc;
    psmtp_usb_module_ops module_ops={out,usbd_is_loaded,usbd_load,usbd_find_handle,usbd_unload};
    if(usb_stopping || psmtp_lifecycle_requested())return 1;
    if(runtime.ready) return 0;
    if(now_ms(NULL)<runtime.retry_at) return 1;
    /* Keep SDK checks for the unchanged declarations. Exit/list/free are
     * intentional corrections, not compatible assignments to the old header.
     * Actual inspected library SHA-256 and call graphs: USB_ABI_1100.md. */
    if(0) {
        api.init=&sceUsbdInit;api.bus=&sceUsbdGetBusNumber;api.address=&sceUsbdGetDeviceAddress;
        api.descriptor=&sceUsbdGetDeviceDescriptor; api.open=&sceUsbdOpen; api.close=&sceUsbdClose;
        api.configuration=&sceUsbdGetConfiguration; api.claim=&sceUsbdClaimInterface;
        api.release=&sceUsbdReleaseInterface; api.control=&sceUsbdControlTransfer; api.bulk=&sceUsbdBulkTransfer;
    }
    fprintf(out,"PS MTP Explorer: EXPERIMENTAL USB ABI; read-only metadata phase.\n");
    fputs("ABI profile: user-library review 9cc9d4b4021b66da; list=signed64; free=list,unref; free/exit=void.\n",out);
    if(psmtp_usb_module_acquire(&runtime.module,&module_ops,out)) goto cleanup;
    module=runtime.module.handle;
    RESOLVE(init,"sceUsbdInit"); RESOLVE(exit,"sceUsbdExit");
    RESOLVE(list,"sceUsbdGetDeviceList"); RESOLVE(free_list,"sceUsbdFreeDeviceList");
    RESOLVE(bus,"sceUsbdGetBusNumber"); RESOLVE(address,"sceUsbdGetDeviceAddress");
    RESOLVE(descriptor,"sceUsbdGetDeviceDescriptor"); RESOLVE(open,"sceUsbdOpen");
    RESOLVE(close,"sceUsbdClose"); RESOLVE(configuration,"sceUsbdGetConfiguration");
    RESOLVE(claim,"sceUsbdClaimInterface"); RESOLVE(release,"sceUsbdReleaseInterface");
    RESOLVE(control,"sceUsbdControlTransfer"); RESOLVE(bulk,"sceUsbdBulkTransfer");
    fprintf(out,"stage=usbd-init begin\n"); fflush(out);
    rc=api.init(); fprintf(out,"stage=usbd-init rc=0x%08" PRIx32 "\n",(uint32_t)rc);
    if(rc) goto cleanup;
    runtime.api=api;runtime.ready=1;return 0;
cleanup:
    runtime.retry_at=now_ms(NULL)+2000;
    (void)psmtp_usb_module_release(&runtime.module,&module_ops,out);return 1;
}

void psmtp_ps4_usb_stop(void){__atomic_store_n(&usb_stopping,1,__ATOMIC_RELEASE);}
int psmtp_ps4_usb_shutdown_report(FILE *out) {
    int first=0;
    psmtp_usb_module_ops ops={out,usbd_is_loaded,usbd_load,usbd_find_handle,usbd_unload};
    psmtp_ps4_usb_stop();
    /* Caller must first join every borrower of this connection. */
    if(runtime.active){first=close_connection(runtime.active,out);runtime.active=NULL;}
    if(runtime.ready) {
        if(runtime.snapshot) {runtime.api.free_list(runtime.snapshot,1);runtime.snapshot=NULL;}
        psmtp_lifecycle_mark("usbd-exit-enter",0);runtime.api.exit();psmtp_lifecycle_mark("usbd-exit-returned",0);runtime.ready=0;fputs("stage=usbd-exit invoked=1 return=void launch-context=1\n",out);
    }
    psmtp_lifecycle_mark("usbd-module-release-enter",0);
    {int rc=psmtp_usb_module_release(&runtime.module,&ops,out);if(rc && !first)first=rc;psmtp_lifecycle_mark("usbd-module-release-returned",rc);}
    memset(&runtime.watch,0,sizeof(runtime.watch));runtime.retry_at=0;return first;
}
void psmtp_ps4_usb_shutdown(FILE *out){(void)psmtp_ps4_usb_shutdown_report(out);}
typedef struct {libusb_device **list;struct libusb_device_descriptor *descriptors;FILE *out;} probe_context;
static int probe_device(void *user,size_t index) {
    probe_context *p=user;libusb_device_handle *h=NULL;usb_mtp_interface selected;int rc;
    const struct libusb_device_descriptor *d=&p->descriptors[index];
    if(d->bDeviceClass==8 || !d->bNumConfigurations || d->bNumConfigurations>8) return 0;
    if(runtime.active && runtime.active->bus==runtime.api.bus(p->list[index]) && runtime.active->address==runtime.api.address(p->list[index]) && runtime.active->vendor==d->idVendor && runtime.active->product==d->idProduct)return 1;
    rc=runtime.api.open(p->list[index],&h);
    if(rc || !h)fprintf(p->out,"stage=usb-probe-open-failed index=%zu native=0x%08" PRIx32 " handle-present=%d\n",index,(uint32_t)rc,h!=NULL);
    if(!rc && h) rc=inspect(&runtime.api,h,d->bNumConfigurations,&selected,p->out,75);
    else rc=-1;
    if(h) runtime.api.close(h);
    fprintf(p->out,"stage=hotplug-probe index=%zu status=%d vid=%04x pid=%04x\n",index,rc,d->idVendor,d->idProduct);
    return rc==0?1:rc==-2?0:-1;
}
static void discover_name(libusb_device *device,const struct libusb_device_descriptor *d,psmtp_watched_device *e,FILE *log,uint64_t now) {
    libusb_device_handle *h=NULL;uint8_t bytes[255];uint16_t lang=0x0409;int32_t rc=-1;size_t i;
    if(!e->mtp || e->name[0] || e->name_attempts>=3 || now<e->name_retry_at) return;
    ++e->name_attempts;e->name_retry_at=now>UINT64_MAX-1000?UINT64_MAX:now+1000;
    if(!d->iProduct) {e->name_attempts=3;return;}
    if(runtime.api.open(device,&h) || !h) goto done;
    rc=runtime.api.control(h,0x80,6,0x300,0,bytes,sizeof(bytes),75);
    if(rc>=4 && rc<=(int32_t)sizeof(bytes) && bytes[1]==3 && bytes[0]>=4 && bytes[0]<=rc && !(bytes[0]&1)) {
        lang=(uint16_t)(bytes[2]|(uint16_t)bytes[3]<<8);
        for(i=2;i<bytes[0];i+=2) if(bytes[i]==9 && bytes[i+1]==4) {lang=0x0409;break;}
    }
    rc=runtime.api.control(h,0x80,6,(uint16_t)(0x300|d->iProduct),lang,bytes,sizeof(bytes),75);
    if(rc>=2 && rc<=(int32_t)sizeof(bytes)) rc=psmtp_usb_label(bytes,(size_t)rc,e->name,sizeof(e->name));
    else rc=-1;
done:
    if(h) runtime.api.close(h);
    fprintf(log,"stage=phone-label source=usb-product available=%d attempt=%u status=%d names-in-log=0\n",e->name[0]!=0,e->name_attempts,rc);
}
int psmtp_ps4_poll_devices(psmtp_ps4_candidate *out,size_t *count,int force,FILE *log) {
    libusb_device **list=NULL;struct libusb_device_descriptor descriptors[128];
    psmtp_device_record records[128];int64_t n;size_t i,j;uint64_t observed;int rc=1,topology=0;probe_context probe;
    if(usb_ready(log)) return 1;
    n=runtime.api.list(&list);
    if(n<0 || n>128 || (n && !list)){fprintf(log,"stage=usb-enumeration-failed native=%" PRId64 " list-present=%d\n",n,list!=NULL);goto done;}
    memset(records,0,sizeof(records));
    for(i=0;i<(size_t)n;++i) {
        const uint8_t *bytes=(const uint8_t *)&descriptors[i];uint32_t signature=2166136261u;
        if(!list[i]) goto done;memset(&descriptors[i],0,sizeof(descriptors[i]));
        int native=runtime.api.descriptor(list[i],&descriptors[i]);
        if(native || descriptors[i].bLength!=18 || descriptors[i].bDescriptorType!=1){fprintf(log,"stage=usb-device-descriptor-failed index=%zu native=%d\n",i,native);goto done;}
        for(j=0;j<sizeof(descriptors[i]);++j) signature=(signature^bytes[j])*16777619u;
        records[i].bus=runtime.api.bus(list[i]);records[i].address=runtime.api.address(list[i]);
        records[i].vendor=descriptors[i].idVendor;records[i].product=descriptors[i].idProduct;records[i].signature=signature;
    }
    observed=now_ms(NULL);probe.list=list;probe.descriptors=descriptors;probe.out=log;
    topology=runtime.watch.count!=(size_t)n;
    for(i=0;i<(size_t)n && !topology;++i) {
        psmtp_device_record *old=&runtime.watch.entries[i].record;
        topology=old->bus!=records[i].bus || old->address!=records[i].address || old->signature!=records[i].signature;
    }
    rc=psmtp_device_watch_update(&runtime.watch,records,(size_t)n,now_ms(NULL),force,probe_device,&probe,out);
    if(!rc) {
        int budget=force?128:1;uint64_t now=now_ms(NULL);
        for(i=0;i<(size_t)n;++i) {
            psmtp_watched_device *e=&runtime.watch.entries[i];
            if(budget && e->mtp && !e->name[0] && e->name_attempts<3 && now>=e->name_retry_at) {discover_name(list[i],&descriptors[i],e,log,now);--budget;}
            memcpy(out[i].name,e->name,sizeof(out[i].name));
        }
        if(topology || force)for(i=0;i<(size_t)n;++i)fprintf(log,"stage=usb-device index=%zu bus=%u address=%u vid=%04x pid=%04x class=%02x configs=%u mtp=%u connection=%" PRIu64 "\n",i,records[i].bus,records[i].address,records[i].vendor,records[i].product,descriptors[i].bDeviceClass,descriptors[i].bNumConfigurations,out[i].mtp,out[i].connection);
        *count=(size_t)n;
        if(topology || force) fprintf(log,"stage=usb-topology timestamp-ms=%" PRIu64 " enumerated=%zu\n",observed,(size_t)n);
    }
done:
    if(n>=0 && n<=128 && (!n || list)) {
        if(runtime.snapshot) runtime.api.free_list(runtime.snapshot,1);
        runtime.snapshot=list;
    } else if(list) runtime.api.free_list(list,1);
    return rc;
}
static void finish_mtp(ps4_io *io,mtp_client *client,FILE *out){
    int rc=0;if(usb_stopping)client->timeout_ms=250;psmtp_lifecycle_mark("mtp-close-session-enter",client->poisoned);if(client->session)rc=mtp_close_session(client);psmtp_lifecycle_mark("mtp-close-session-returned",rc);
    fprintf(out,"stage=mtp-release close-status=%d poisoned=%d session=%d\n",rc,client->poisoned,client->session);
    /* An incomplete bulk transaction cannot carry CloseSession safely. The
     * MTP class reset is confined to the claimed interface before release;
     * it is never a USB bus/device reset or a replay of a file operation. */
    if(client->poisoned || client->session_cleanup_required){
        int32_t reset=io->api->control(io->handle,0x21,0x66,0,io->selected.interface_number,NULL,0,usb_stopping?250:2000);
        fprintf(out,"stage=mtp-release class-reset=0x%08" PRIx32 " interface=%u\n",(uint32_t)reset,io->selected.interface_number);
    }
}
static int restore_alternate(usbd_api *api,libusb_device_handle *handle,const usb_mtp_interface *selected,uint8_t old,int changed,FILE *out){
    if(!changed)return 0;
    int rc=api->control(handle,0x01,11,old,selected->interface_number,NULL,0,usb_stopping?250:2000);
    fprintf(out,"stage=restore-interface interface=%u alternate=%u native=%d\n",selected->interface_number,old,rc);return rc?MTP_IO:0;
}
static int close_connection(ps4_io *io,FILE *out){
    int rc=0;
    if(io->client){finish_mtp(io,io->client,out);free(io->client);io->client=NULL;}
    if(io->claimed){rc=restore_alternate(io->api,io->handle,&io->selected,io->previous_alt,io->restore_alt,out);io->restore_alt=0;}
    if(io->claimed){psmtp_lifecycle_mark("usb-release-interface-enter",io->selected.interface_number);int released=io->api->release(io->handle,io->selected.interface_number);if(!rc)rc=released;fprintf(out,"stage=release rc=0x%08" PRIx32 "\n",(uint32_t)released);io->claimed=0;psmtp_lifecycle_mark("usb-release-interface-returned",released);}
    if(io->handle){psmtp_lifecycle_mark("usb-close-handle-enter",0);io->api->close(io->handle);psmtp_lifecycle_mark("usb-close-handle-returned",0);io->handle=NULL;fputs("stage=close invoked=1 (void return)\n",out);}
    if(io->devices){io->api->free_list(io->devices,1);io->devices=NULL;}
    return rc;
}

/* Android Open Accessory data only. Identify and avoid an ADB interface. */
static int accessory_device(const struct libusb_device_descriptor *d) {
    return d->idVendor==0x18d1 && (d->idProduct==0x2d00 || d->idProduct==0x2d01);
}

static int accessory_claim(psmtp_android_accessory *out, libusb_device *device,
                           const struct libusb_device_descriptor *descriptor, FILE *log) {
    libusb_device_handle *handle=NULL;
    uint8_t bytes[USB_MAX_CONFIG_BYTES];
    usb_alt alternatives[USB_MAX_ALTS];
    int32_t configuration;
    int rc=MTP_UNSUPPORTED;
    out->stage="opening the accessory";
    out->native_status=runtime.api.open(device,&handle);
    if(out->native_status || !handle) {
        if(handle) runtime.api.close(handle);
        return MTP_IO;
    }
    out->stage="reading the accessory configuration";
    out->native_status=runtime.api.configuration(handle,&configuration);
    if(out->native_status || configuration<=0 || configuration>255 ||
       !descriptor->bNumConfigurations || descriptor->bNumConfigurations>8) goto done;
    for(uint16_t i=0;i<descriptor->bNumConfigurations;++i) {
        size_t count;
        uint8_t value;
        int32_t got=runtime.api.control(handle,0x80,6,(uint16_t)(0x200|i),0,bytes,9,250);
        out->native_status=got;
        if(got!=9 || bytes[0]!=9 || bytes[1]!=2) goto done;
        if(bytes[5]!=configuration) continue;
        uint16_t size=(uint16_t)(bytes[2]|(unsigned)bytes[3]<<8);
        if(size<9 || size>sizeof(bytes)) goto done;
        out->native_status=runtime.api.control(handle,0x80,6,(uint16_t)(0x200|i),0,bytes,size,250);
        if(out->native_status!=size ||
           usb_parse_config(bytes,size,alternatives,&count,&value) || value!=configuration) goto done;
        out->stage="finding the accessory data interface";
        out->native_status=0;
        for(size_t j=0;j<count;++j) {
            const usb_alt *alt=&alternatives[j];
            uint8_t input=0,output=0;
            fprintf(log,"stage=usb-aoa-interface index=%u alt=%u class=%02x/%02x/%02x endpoints=%zu\n",
                alt->number,alt->alternate,alt->cls,alt->subclass,alt->protocol,alt->endpoint_count);
            /* AOA is ff/ff/00. This excludes the ADB ff/42/01 interface
             * even if the gadget assigns it interface zero. */
            if(alt->alternate || alt->cls!=0xff || alt->subclass!=0xff || alt->protocol || alt->endpoint_count!=2) continue;
            for(size_t endpoint=0;endpoint<alt->endpoint_count;++endpoint) {
                const usb_endpoint *ep=&alt->endpoints[endpoint];
                if((ep->attributes&3)!=2 || !(ep->address&15) || (ep->address&0x70) ||
                   !ep->max_packet || ep->max_packet>1024) break;
                if(ep->address&0x80) input=ep->address;
                else output=ep->address;
            }
            if(!input || !output) goto done;
            out->stage="claiming the accessory data interface";
            out->native_status=runtime.api.claim(handle,alt->number);
            if(out->native_status) {rc=MTP_IO;goto done;}
            out->handle=handle;out->interface_number=alt->number;out->bulk_in=input;out->bulk_out=output;
            fprintf(log,"stage=usb-aoa-claim interface=%u in=%02x out=%02x status=0\n",alt->number,input,output);
            return MTP_OK;
        }
        break;
    }
done:
    runtime.api.close(handle);
    return rc;
}

static int accessory_switch(psmtp_android_accessory *out,libusb_device *device,FILE *log) {
    static const char *const identity[]={"nueex", "PS MTP Explorer", "USB Direct Download", "2"};
    libusb_device_handle *handle=NULL;
    uint8_t version[2];
    int rc=MTP_UNSUPPORTED;
    out->stage="opening the phone";
    out->native_status=runtime.api.open(device,&handle);
    if(out->native_status || !handle) {
        if(handle) runtime.api.close(handle);
        return MTP_IO;
    }
    out->stage="querying Android accessory support";
    out->native_status=runtime.api.control(handle,0xc0,51,0,0,version,sizeof(version),250);
    fprintf(log,"stage=usb-aoa-control request=version result=%" PRId32 "\n",out->native_status);
    if(out->native_status!=2 || !(version[0] || version[1])) goto done;
    out->stage="sending the accessory identity";
    for(uint16_t i=0;i<sizeof(identity)/sizeof(*identity);++i) {
        uint16_t length=(uint16_t)(strlen(identity[i])+1);
        out->native_status=runtime.api.control(handle,0x40,52,0,i,(unsigned char *)(uintptr_t)identity[i],length,250);
        fprintf(log,"stage=usb-aoa-control request=identity index=%u result=%" PRId32 " expected=%u\n",i,out->native_status,length);
        if(out->native_status!=length) goto done;
    }
    out->stage="waiting for Android accessory mode";
    out->native_status=runtime.api.control(handle,0x40,53,0,0,NULL,0,500);
    fprintf(log,"stage=usb-aoa-control request=start result=%" PRId32 "\n",out->native_status);
    /* Re-enumeration can interrupt the control call. A failed START is
     * accepted only when the expected accessory actually reappears. */
    rc=MTP_OK;
done:
    runtime.api.close(handle);
    return rc;
}

static int watched_mtp(const psmtp_device_record *key) {
    for(size_t i=0;i<runtime.watch.count;++i) {
        const psmtp_watched_device *entry=&runtime.watch.entries[i];
        if(entry->mtp && entry->record.bus==key->bus && entry->record.address==key->address &&
           entry->record.vendor==key->vendor && entry->record.product==key->product) return 1;
    }
    return 0;
}

static int same_usb_device(const psmtp_device_record *a,const psmtp_device_record *b) {
    return a->signature && b->signature && a->bus==b->bus && a->address==b->address &&
        a->vendor==b->vendor && a->product==b->product && a->signature==b->signature;
}

/* When the native library exposes no physical port path, switch one phone
 * and accept only a one-for-one replacement in the pre-switch USB list. */
static int accessory_replacement(const psmtp_device_record *before,const psmtp_device_record *after,
                                 size_t count,size_t phone,size_t accessory) {
    for(size_t i=0;i<count;++i) {
        if(!before[i].signature || !after[i].signature) return 0;
        int found=0;
        for(size_t j=0;j<count;++j) {
            if(j!=accessory && same_usb_device(before+i,after+j)) {found=1;break;}
        }
        if(found!=(i!=phone)) return 0;
    }
    return 1;
}

int psmtp_accessory_open(psmtp_android_accessory *out,int (*cancelled)(void *),void *user,FILE *log) {
    int32_t (*ports)(libusb_device *,uint8_t *,int32_t)=NULL;
    psmtp_device_record before[PSMTP_WATCH_MAX];
    size_t count_before=0,phone=0;
    uint8_t old_ports[8],bus=0;
    int32_t depth=0;
    uint64_t switched_at=0;
    out->stage="preparing the PS4 USB service";
    if(out->handle || runtime.active) return MTP_STATE;
    if(cancelled(user)) return MTP_CANCELLED;
    if(usb_ready(log)) return MTP_UNSUPPORTED;

    for(;;) {
        libusb_device **devices=NULL;
        struct libusb_device_descriptor selected={0};
        psmtp_device_record keys[PSMTP_WATCH_MAX]={0};
        size_t candidate=0;
        unsigned candidates=0;
        int rc=MTP_DISCONNECTED;
        if(cancelled(user)) return MTP_CANCELLED;
        int64_t count=runtime.api.list(&devices);
        if(count<0 || count>PSMTP_WATCH_MAX || (count && !devices)) {
            if(devices) runtime.api.free_list(devices,1);
            out->stage="listing USB devices";
            return MTP_IO;
        }
        for(int64_t i=0;i<count;++i) {
            struct libusb_device_descriptor d={0};
            if(!devices[i] || runtime.api.descriptor(devices[i],&d) || d.bLength!=18 || d.bDescriptorType!=1) continue;
            uint32_t signature=2166136261u;
            const uint8_t *bytes=(const uint8_t *)&d;
            for(size_t j=0;j<sizeof(d);++j) signature=(signature^bytes[j])*16777619u;
            keys[i]=(psmtp_device_record){runtime.api.bus(devices[i]),runtime.api.address(devices[i]),d.idVendor,d.idProduct,signature};
            int accessory=accessory_device(&d);
            if(switched_at) {
                uint8_t current[8];
                if(!accessory) continue;
                if(depth>0 && (keys[i].bus!=bus || ports(devices[i],current,sizeof(current))!=depth ||
                   memcmp(old_ports,current,(size_t)depth))) continue;
            } else {
                fprintf(log,"stage=usb-aoa-device bus=%u address=%u vid=%04x pid=%04x accessory=%d\n",
                    keys[i].bus,keys[i].address,d.idVendor,d.idProduct,accessory);
                if(!accessory && !watched_mtp(&keys[i])) continue;
            }
            candidate=(size_t)i;selected=d;++candidates;
        }
        if(candidates>1) {
            out->stage="choosing a single Android phone";
            rc=MTP_STATE;
        } else if(candidates && accessory_device(&selected)) {
            if(!switched_at || depth>0 || ((size_t)count==count_before && accessory_replacement(before,keys,count_before,phone,candidate))) {
                fprintf(log,"stage=usb-aoa-match method=%s bus=%u address=%u vid=%04x pid=%04x\n",
                    !switched_at?"already-accessory":depth>0?"port":"topology",keys[candidate].bus,keys[candidate].address,selected.idVendor,selected.idProduct);
                rc=accessory_claim(out,devices[candidate],&selected,log);
            }
        } else if(candidates) {
            if(!resolve(runtime.module.handle,&ports,sizeof(ports),"sceUsbdGetPortNumbers",log)) {
                bus=keys[candidate].bus;
                depth=ports(devices[candidate],old_ports,sizeof(old_ports));
                if(depth<0 || (size_t)depth>sizeof(old_ports)) depth=0;
            }
            count_before=(size_t)count;phone=candidate;
            memcpy(before,keys,count_before*sizeof(*before));
            fprintf(log,"stage=usb-aoa-selection method=%s bus=%u address=%u devices=%zu\n",
                depth>0?"port":"topology",keys[phone].bus,keys[phone].address,count_before);
            rc=accessory_switch(out,devices[candidate],log);
            if(!rc) switched_at=now_ms(NULL);
        } else if(!switched_at) {
            out->stage="finding an Android phone in File Transfer mode";
        }
        if(out->handle) {
            out->devices=devices;out->bus=keys[candidate].bus;out->address=keys[candidate].address;
            return MTP_OK;
        }
        if(devices) runtime.api.free_list(devices,1);
        if((rc!=MTP_DISCONNECTED && rc!=MTP_OK) || !switched_at) return rc;
        if(now_ms(NULL)-switched_at>=7000) return MTP_TIMEOUT;
#ifndef PSMTP_TEST_USB_RUNTIME
        sceKernelUsleep(75000);
#endif
    }
}

int psmtp_accessory_transfer(psmtp_android_accessory *link,int sending,void *bytes,size_t n,size_t *got,uint32_t timeout) {
    int32_t actual=0;
    *got=0;
    if(!link->handle || n>INT32_MAX) return MTP_STATE;
    if(__atomic_load_n(&usb_stopping,__ATOMIC_ACQUIRE) || psmtp_lifecycle_requested()) return MTP_CANCELLED;
    int32_t rc=runtime.api.bulk(link->handle,sending?link->bulk_out:link->bulk_in,bytes,(int32_t)n,&actual,timeout);
    if(rc) link->native_status=rc;
    if(actual<0 || (size_t)actual>n) return MTP_PROTOCOL;
    *got=(size_t)actual;
    return rc?MTP_IO:MTP_OK;
}

int psmtp_accessory_present(const psmtp_android_accessory *link) {
    libusb_device **devices=NULL;
    int rc=MTP_DISCONNECTED;
    if(!link->handle) return MTP_STATE;
    int64_t count=runtime.api.list(&devices);
    if(count<0 || count>PSMTP_WATCH_MAX || (count && !devices)) rc=MTP_IO;
    else for(int64_t i=0;i<count;++i) {
        struct libusb_device_descriptor d={0};
        if(!devices[i] || runtime.api.bus(devices[i])!=link->bus || runtime.api.address(devices[i])!=link->address) continue;
        if(!runtime.api.descriptor(devices[i],&d) && accessory_device(&d)) rc=MTP_OK;
        break;
    }
    if(devices) runtime.api.free_list(devices,1);
    return rc;
}

int psmtp_accessory_close(psmtp_android_accessory *link) {
    if(!link->handle) return MTP_OK;
    int32_t released=runtime.api.release(link->handle,link->interface_number);
    runtime.api.close(link->handle);
    if(link->devices) runtime.api.free_list(link->devices,1);
    memset(link,0,sizeof(*link));
    return released?MTP_IO:MTP_OK;
}

int psmtp_ps4_diagnose(const psmtp_ps4_options *options,FILE *out) {
    usbd_api api;libusb_device **devices=NULL;libusb_device_handle *handle=NULL;
    int32_t count=0,rc;int64_t list_count;int claimed=0,result=1,index,alt_changed=0;uint8_t previous_alt=0;size_t n=0,i;
    psmtp_ps4_candidate candidates[128]={{0}};usb_mtp_interface selected;
    mtp_client *client=NULL;ps4_io io={0};mtp_transport transport;
    if(psmtp_ps4_poll_devices(candidates,&n,1,out)) return 1;
    api=runtime.api;
    /* Hold a separate list for this connection: idle polls can replace their
     * temporary wrapper objects without invalidating the live handle's pin. */
    list_count=api.list(&devices);
    if(list_count<0 || list_count>128 || (list_count && !devices)) goto cleanup;
    count=(int32_t)list_count;memset(candidates,0,sizeof(candidates));
    for(index=0;index<count;++index) {
        struct libusb_device_descriptor d={0};uint8_t bus,address;
        if(!devices[index] || api.descriptor(devices[index],&d)) goto cleanup;
        bus=api.bus(devices[index]);address=api.address(devices[index]);
        for(i=0;i<runtime.watch.count;++i) {
            psmtp_watched_device *e=&runtime.watch.entries[i];
            if(e->record.bus==bus && e->record.address==address && e->record.vendor==d.idVendor && e->record.product==d.idProduct) {
                candidates[index].vendor_id=d.idVendor;candidates[index].product_id=d.idProduct;candidates[index].mtp=(uint8_t)e->mtp;candidates[index].connection=e->connection;memcpy(candidates[index].name,e->name,sizeof(candidates[index].name));break;
            }
        }
    }
    if(options->devices) options->devices(options->user,candidates,(size_t)count);
    if(!options->communicate) { fputs("stage=discovery complete; no interface claimed and no MTP sent.\n",out); result=0; goto cleanup; }
    for(index=0;index<count && !candidates[index].mtp;++index) {}
    if(index==count) { fputs("stage=choose-device failed: no identified MTP candidate. Dependent steps SKIPPED.\n",out); goto cleanup; }
    if(!options->choose_device) goto cleanup;
    index=options->choose_device(options->user,candidates,(size_t)count);
    if(index<0) { fputs("stage=choose-device cancelled; no MTP sent.\n",out); result=0; goto cleanup; }
    if(index>=count || !candidates[index].mtp) goto cleanup;
    {
        struct libusb_device_descriptor d;
        memset(&d,0,sizeof(d));
        rc=api.descriptor(devices[index],&d);
        fprintf(out,"stage=selected-descriptor index=%d rc=0x%08" PRIx32 " vid=%04x pid=%04x\n",index,(uint32_t)rc,d.idVendor,d.idProduct);
        if(rc || d.bLength!=18 || d.bDescriptorType!=1) goto cleanup;
        rc=api.open(devices[index],&handle);
        fprintf(out,"stage=selected-open index=%d rc=0x%08" PRIx32 "\n",index,(uint32_t)rc);
        if(rc || !handle || inspect(&api,handle,d.bNumConfigurations,&selected,out,2000)) goto cleanup;
        io.bus=api.bus(devices[index]);io.address=api.address(devices[index]);io.vendor=d.idVendor;io.product=d.idProduct;
    }
    fprintf(out,"stage=selected-link interface=%u bulk-in=%02x in-packet=%u bulk-out=%02x out-packet=%u stream-read=%u\n",selected.interface_number,selected.bulk_in,selected.in_max_packet,selected.bulk_out,selected.out_max_packet,MTP_STREAM_CHUNK);
    fprintf(out,"stage=claim begin interface=%u\n",selected.interface_number); fflush(out);
    rc=api.claim(handle,selected.interface_number);
    fprintf(out,"stage=claim interface=%u rc=0x%08" PRIx32 "\n",selected.interface_number,(uint32_t)rc);
    if(rc) goto cleanup;
    claimed=1;
    {
        uint8_t alt=0xff;
        fputs("stage=get-interface begin\n",out); fflush(out);
        rc=api.control(handle,0x81,10,0,selected.interface_number,&alt,1,2000);
        fprintf(out,"stage=get-interface rc=%" PRId32 " alternate=%u\n",rc,alt);
        if(rc!=1)goto cleanup;
        previous_alt=alt;
        if(alt!=selected.alternate){
            alt_changed=1;
            rc=api.control(handle,0x01,11,selected.alternate,selected.interface_number,NULL,0,2000);
            fprintf(out,"stage=set-interface interface=%u alternate=%u native=%d\n",selected.interface_number,selected.alternate,rc);
            if(rc)goto cleanup;
            rc=api.control(handle,0x81,10,0,selected.interface_number,&alt,1,2000);
            fprintf(out,"stage=verify-interface native=%d alternate=%u expected=%u\n",rc,alt,selected.alternate);
            if(rc!=1 || alt!=selected.alternate)goto cleanup;
        }
    }
    if(now_ms(NULL)==UINT64_MAX) { fprintf(out,"monotonic clock unavailable\n"); goto cleanup; }
    client=(mtp_client *)malloc(sizeof(*client)); if(!client) goto cleanup;
    rc=api.configuration(handle,&io.configuration);if(rc || io.configuration<0)goto cleanup;
    io.api=&api; io.handle=handle; io.selected=selected; io.log=out;
    transport.user=&io; transport.read=read_bulk; transport.write=write_bulk; transport.now_ms=now_ms;
    mtp_init(client,transport);client->out_max_packet=selected.out_max_packet; io.client=client;
    client->resume_check=resume_connection;
    fprintf(out,"stage=mtp-transport-ready interface=%u alternate=%u timeout-ms=%" PRIu32 "\n",selected.interface_number,selected.alternate,client->timeout_ms);
    io.devices=devices;io.claimed=claimed;io.previous_alt=previous_alt;io.restore_alt=alt_changed;runtime.active=&io;
#ifdef PSMTP_PRODUCTION
    result=options->browse?!!options->browse(options->user,client,out):1;
#else
    result=(options->browse?options->browse(options->user,client,out):mtp_diagnose_roots(client,out,options->names))?1:0;
#endif
cleanup:
    if(io.api){
        rc=close_connection(&io,out);if(runtime.active==&io)runtime.active=NULL;
        if(rc)result=1;client=NULL;handle=NULL;devices=NULL;claimed=0;
    }
    if(client)free(client);
    if(claimed && alt_changed)(void)restore_alternate(&api,handle,&selected,previous_alt,alt_changed,out);
    if(claimed) { fputs("stage=release begin\n",out); fflush(out); rc=api.release(handle,selected.interface_number); fprintf(out,"stage=release rc=0x%08" PRIx32 "\n",(uint32_t)rc); if(rc) result=1; }
    if(handle) { api.close(handle); fprintf(out,"stage=close invoked=1 (void return)\n"); }
    if(devices && api.free_list) {
        fputs("stage=free-device-list begin unref=1 return=void (reviewed native implementation)\n",out); fflush(out);
        api.free_list(devices,1); /* release the list-owned device references */
        devices=NULL;
        fputs("stage=free-device-list invoked=1 returned=1; no status return\n",out);
    }

    fprintf(out,"stage=diagnostic-end status=%d; see all prior stages; hardware result must be reported.\n",result);
    fflush(out); return result;
}
