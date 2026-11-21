/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#include "lan_server.h"
#include "psmtp/direct_download.h"
#include "psmtp/lan_spake.h"
#include "lan_identity.h"
#include "monotonic.h"
#include "optional_module.h"
#include "qrcodegen.h"
#define sceNetErrnoLoc psmtp_sdk_errno_placeholder
#include <orbis/Net.h>
#undef sceNetErrnoLoc
#include <orbis/NetCtl.h>
#include <orbis/libkernel.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int *sceNetErrnoLoc(void);
#define LAN_PORT 41984u
#define LAN_LEVEL_SOCKET 0xffff
#define LAN_NONBLOCK 0x1200
#define LAN_REUSEADDR 4
#define LAN_RCVBUF 0x1002
#define LAN_RECEIVE_BUFFER (512*1024)
#define LAN_SEND_NOSIGNAL 0x20000
#define LAN_MSG_PEEK 2
#define NET_AGAIN ((int32_t)UINT32_C(0x80410123))
#define NET_INTR ((int32_t)UINT32_C(0x80410104))

struct psmtp_lan_server {
    psmtp_work *work;
    int listener, peer, pool, acquired, random_owned, manual;
    int32_t (*random)(void *,size_t);
    uint64_t until, transfer_started, wire_in, waits;
    psmtp_lan_pair_ui *pairing;
    br_ssl_server_context tls;
    br_sslio_context io;
    unsigned char buffer[BR_SSL_BUFSIZE_BIDI];
};

static unsigned net_users;
static int net_lock;
static psmtp_lan_identity identity;
static int identity_ready;

void psmtp_lan_forget(void){
    volatile unsigned char *bytes=(volatile unsigned char *)&identity;
    for(size_t i=0;i<sizeof(identity);++i)bytes[i]=0;
    identity_ready=0;
}

static void lock_network(void){
    while(__atomic_exchange_n(&net_lock,1,__ATOMIC_ACQUIRE))sceKernelUsleep(1000);
}

int psmtp_lan_net_acquire(int (*init)(void)){
    int rc;
    lock_network();
    rc=net_users?0:init();
    if(!rc)++net_users;
    __atomic_store_n(&net_lock,0,__ATOMIC_RELEASE);
    return rc;
}

void psmtp_lan_net_release(void (*term)(void)){
    lock_network();
    if(net_users && !--net_users)term();
    __atomic_store_n(&net_lock,0,__ATOMIC_RELEASE);
}

static void hex_token(char out[33],const unsigned char bytes[16]){
    static const char hex[]="0123456789abcdef";
    for(size_t i=0;i<16;++i){out[i*2]=hex[bytes[i]>>4];out[i*2+1]=hex[bytes[i]&15];}
    out[32]=0;
}

static uint64_t now(void){
    struct timespec clock;
    return psmtp_monotonic_time(&clock)?0:(uint64_t)clock.tv_sec*1000+(uint64_t)clock.tv_nsec/1000000;
}

static int nonblocking(int fd){
    int yes=1;
    return sceNetSetsockopt(fd,LAN_LEVEL_SOCKET,LAN_NONBLOCK,&yes,sizeof(yes));
}

static int retry(int rc){
    if(rc==-1){int *err=sceNetErrnoLoc();return err && (*err==35 || *err==4);}
    return rc==NET_AGAIN || rc==NET_INTR || rc==-35 || rc==-4;
}

static int link_io(void *context,unsigned char *buffer,size_t size,int outgoing){
    psmtp_lan_server *server=context;
    unsigned delay=250;
    for(;;){
        uint64_t clock=now();
        if(psmtp_work_cancelled(server->work) || !clock || clock>=server->until ||
            (server->manual && !__atomic_load_n(&server->pairing->visible,__ATOMIC_ACQUIRE)))return -1;
        int rc=outgoing?sceNetSend(server->peer,buffer,size,LAN_SEND_NOSIGNAL):sceNetRecv(server->peer,buffer,size,0);
        if(rc>0){if(!outgoing)server->wire_in+=(uint64_t)rc;return rc;}
        if(!retry(rc))return -1;
        ++server->waits;
        sceKernelUsleep(delay);
        if(delay<2000)delay*=2;
    }
}

static int net_read(void *context,unsigned char *buffer,size_t size){return link_io(context,buffer,size,0);}
static int net_write(void *context,const unsigned char *buffer,size_t size){return link_io(context,(unsigned char *)buffer,size,1);}

static int lan_address(unsigned char address[16],char text[16]){
    OrbisNetCtlInfo ctl={0};
    int rc=sceNetCtlInit();
    if(rc)return MTP_DISCONNECTED;
    rc=sceNetCtlGetInfo(ORBIS_NET_CTL_INFO_IP_ADDRESS,&ctl);
    sceNetCtlTerm();
    if(rc || !memchr(ctl.ip_address,0,sizeof(ctl.ip_address)) || sceNetInetPton(2,ctl.ip_address,address+4)!=1)
        return MTP_DISCONNECTED;
    const unsigned char *ip=address+4;
    if(ip[0]!=10 && !(ip[0]==172 && ip[1]>=16 && ip[1]<=31) &&
        !(ip[0]==192 && ip[1]==168) && !(ip[0]==169 && ip[1]==254))return MTP_UNSUPPORTED;
    memcpy(text,ctl.ip_address,16);
    return MTP_OK;
}

static int load_random(psmtp_lan_server *server){
    FILE *log=psmtp_work_log(server->work);
    int module=psmtp_optional_module("libSceRandom");
    if(module<0){
        int32_t status=sceSysmoduleLoadModule(ORBIS_SYSMODULE_RANDOM);
        fprintf(log,"stage=lan-random-load native=0x%08x\n",(unsigned)status);
        if(status)return MTP_UNSUPPORTED;
        server->random_owned=1;
        module=psmtp_optional_module("libSceRandom");
    }
    int rc=psmtp_optional_symbol(module,"sceRandomGetRandomNumber",&server->random,sizeof(server->random));
    fprintf(log,"stage=lan-random-resolve status=%d\n",rc);
    return rc;
}

int psmtp_lan_open(psmtp_lan_server **out,psmtp_work *work,char host[16],unsigned char qr[PSMTP_LAN_QR_BYTES],
    psmtp_lan_pair_ui *pairing){
    unsigned char address[16]={16,2,LAN_PORT>>8,LAN_PORT&255};
    int rc, yes=1;
    psmtp_lan_server *server=calloc(1,sizeof(*server));
    if(!server)return MTP_MEMORY;
    server->listener=server->peer=server->pool=-1;server->work=work;server->pairing=pairing;*out=server;
    rc=psmtp_lan_net_acquire(sceNetInit);
    if(rc)return MTP_IO;
    server->acquired=1;
    server->pool=sceNetPoolCreate("psmtp-lan",1024*1024,0);
    if(server->pool<0)return MTP_IO;
    if((rc=lan_address(address,host))!=0)return rc;
    if((rc=load_random(server))!=0)return rc;
    if(!identity_ready){
        unsigned char seed[64];
        int32_t status=server->random(seed,sizeof(seed));
        fprintf(psmtp_work_log(work),"stage=lan-random-seed native=0x%08x\n",(unsigned)status);
        if(status || psmtp_lan_identity_make(&identity,seed)){
            memset(seed,0,sizeof(seed));return MTP_IO;
        }
        memset(seed,0,sizeof(seed));
        identity_ready=1;
    }
    server->listener=sceNetSocket("psmtp-lan",2,1,0);
    if(server->listener<0 || nonblocking(server->listener))return MTP_IO;
    int receive_buffer=LAN_RECEIVE_BUFFER;
    int buffer_rc=sceNetSetsockopt(server->listener,LAN_LEVEL_SOCKET,LAN_RCVBUF,&receive_buffer,sizeof(receive_buffer));
    fprintf(psmtp_work_log(work),"stage=lan-receive-window requested=%d native=0x%08x\n",receive_buffer,(unsigned)buffer_rc);
    if(
        sceNetSetsockopt(server->listener,LAN_LEVEL_SOCKET,LAN_REUSEADDR,&yes,sizeof(yes)) ||
        sceNetBind(server->listener,(const OrbisNetSockaddr *)address,sizeof(address)) ||
        sceNetListen(server->listener,2))return MTP_IO;
    char pin[33],token[33],payload[96];
    uint8_t scratch[PSMTP_LAN_QR_BYTES];
    hex_token(pin,identity.pin);hex_token(token,identity.token);
    int length=snprintf(payload,sizeof(payload),"PSMTP1:%s:%s%s",host,pin,token);
    if(length<0 || (size_t)length>=sizeof(payload) ||
       !qrcodegen_encodeText(payload,scratch,qr,qrcodegen_Ecc_MEDIUM,1,PSMTP_LAN_QR_VERSION,qrcodegen_Mask_AUTO,true))
        return MTP_LIMIT;
    return MTP_OK;
}

static int short_pair(psmtp_lan_server *server){
    uint8_t commitment[32], check[32], phone[16], console[16], typed[4];
    psmtp_lan_pair_ui *pairing=server->pairing;
    if(!__atomic_load_n(&pairing->visible,__ATOMIC_ACQUIRE))return MTP_CANCELLED;
    if(br_sslio_read_all(&server->io,commitment,sizeof(commitment)) ||
        server->random(console,sizeof(console)) || psmtp_lan_write(server,console,sizeof(console)) ||
        br_sslio_read_all(&server->io,phone,sizeof(phone)))return MTP_IO;
    psmtp_lan_commit(identity.pin,phone,check);
    if(!psmtp_lan_pair_equal(commitment,check,sizeof(check)))return MTP_VERIFY;
    unsigned code=psmtp_lan_pair_code(identity.pin,phone,console);
    __atomic_store_n(&pairing->code,(int)code+1,__ATOMIC_RELEASE);
    server->until=now()+55000;
    if(br_sslio_read_all(&server->io,typed,sizeof(typed)))return MTP_DISCONNECTED;
    uint32_t entered=(uint32_t)typed[0]<<24 | (uint32_t)typed[1]<<16 | (uint32_t)typed[2]<<8 | typed[3];
    if(entered!=code)return MTP_VERIFY;
    __atomic_store_n(&pairing->decision,1,__ATOMIC_RELEASE);
    while(now()<server->until && !psmtp_work_cancelled(server->work) &&
            __atomic_load_n(&pairing->visible,__ATOMIC_ACQUIRE)){
        if(__atomic_load_n(&pairing->decision,__ATOMIC_ACQUIRE)==2){
            uint8_t reply[24]={'P','S','R','1'};
            memcpy(reply+8,identity.token,sizeof(identity.token));
            return psmtp_lan_write(server,reply,sizeof(reply));
        }
        sceKernelUsleep(20000);
    }
    return MTP_CANCELLED;
}

static int pin_pair(psmtp_lan_server *server) {
    uint8_t scalar[32], phone[65], console[65], supplied[32], proofs[64];
    uint8_t response[56] = {'P', 'S', 'R', '1'};
    unsigned code;
    if (!__atomic_load_n(&server->pairing->visible, __ATOMIC_ACQUIRE)) return MTP_CANCELLED;
    do {
        uint32_t random;
        if (server->random(&random, sizeof(random))) return MTP_IO;
        code = random & 0xfffff;
    } while (code >= 1000000);
    do {
        if (server->random(scalar, sizeof(scalar))) return MTP_IO;
    } while (!psmtp_spake_scalar(scalar));
    int rc = MTP_VERIFY;
    if (psmtp_spake_share(code, scalar, 0, console)) {
        __atomic_store_n(&server->pairing->code, (int)code + 1, __ATOMIC_RELEASE);
        rc = psmtp_lan_write(server, console, sizeof(console));
        server->until = now() + 55000;
        if (!rc && (br_sslio_read_all(&server->io, phone, sizeof(phone)) ||
                    br_sslio_read_all(&server->io, supplied, sizeof(supplied)))) rc = MTP_DISCONNECTED;
        if (!rc && (!psmtp_spake_confirm(code, scalar, 0, phone, console, identity.pin, proofs) ||
                    !psmtp_lan_pair_equal(supplied, proofs, sizeof(supplied)))) rc = MTP_VERIFY;
        if (!rc) {
            memcpy(response + 8, proofs + 32, 32);
            memcpy(response + 40, identity.token, sizeof(identity.token));
            rc = psmtp_lan_write(server, response, sizeof(response));
        }
    }
    psmtp_spake_clear(scalar, sizeof(scalar));
    psmtp_spake_clear(proofs, sizeof(proofs));
    psmtp_spake_clear(response, sizeof(response));
    return rc;
}

int psmtp_lan_accept(psmtp_lan_server *server){
    static const uint16_t suites[]={BR_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256,
        BR_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256};
    if(server->peer>=0){
        if(sceNetSocketClose(server->peer))return MTP_IO;
        server->peer=-1;
        server->transfer_started=0;
    }
    while(!psmtp_work_cancelled(server->work)){
        int fd=sceNetAccept(server->listener,NULL,NULL);
        if(fd<0){
            if(!retry(fd))return MTP_IO;
            sceKernelUsleep(20000);
            continue;
        }
        unsigned char seed[32], greeting[21];
        server->peer=fd;
        if(nonblocking(fd) || server->random(seed,sizeof(seed)))goto reject;
        int yes=1,receive_buffer=LAN_RECEIVE_BUFFER;
        (void)sceNetSetsockopt(fd,6,1,&yes,sizeof(yes)); /* TCP_NODELAY */
        (void)sceNetSetsockopt(fd,LAN_LEVEL_SOCKET,LAN_RCVBUF,&receive_buffer,sizeof(receive_buffer));
        br_ssl_server_init_full_ec(&server->tls,&identity.chain,1,BR_KEYTYPE_EC,&identity.key);
        br_ssl_engine_set_versions(&server->tls.eng,BR_TLS12,BR_TLS12);
        br_ssl_engine_set_suites(&server->tls.eng,suites,sizeof(suites)/sizeof(suites[0]));
        br_ssl_engine_add_flags(&server->tls.eng,BR_OPT_ENFORCE_SERVER_PREFERENCES);
        static const char *protocols[]={PSMTP_DOWNLOAD_PARTS_ALPN};
        br_ssl_engine_set_protocol_names(&server->tls.eng,protocols,1);
        br_ssl_engine_set_buffer(&server->tls.eng,server->buffer,sizeof(server->buffer),1);
        br_ssl_engine_inject_entropy(&server->tls.eng,seed,sizeof(seed));
        memset(seed,0,sizeof(seed));
        if(!br_ssl_server_reset(&server->tls))goto reject;
        br_sslio_init(&server->io,&server->tls.eng,net_read,server,net_write,server);
        server->until=now()+5000;
        if(br_sslio_read_all(&server->io,greeting,4))goto reject;
        if(!memcmp(greeting,"PSM3",4) || !memcmp(greeting,"PSM2",4)){
            server->manual=1;
            int rc=greeting[3]=='3'?pin_pair(server):short_pair(server);
            server->manual=0;
            __atomic_store_n(&server->pairing->decision,0,__ATOMIC_RELEASE);
            __atomic_store_n(&server->pairing->code,0,__ATOMIC_RELEASE);
            fprintf(psmtp_work_log(server->work),"stage=lan-manual-pair status=%d\n",rc);
            goto reject;
        }
        if(memcmp(greeting,"PSL1",4) || br_sslio_read_all(&server->io,greeting+4,17) ||
            !psmtp_lan_pair_equal(greeting+4,identity.token,sizeof(identity.token)) || greeting[20]>1)goto reject;
        if(psmtp_lan_write(server,"PSR1\0\0\0\0",8))goto reject;
        if(greeting[20]==1){
            br_ssl_session_parameters session;
            br_ssl_engine_get_session_parameters(&server->tls.eng,&session);
            server->transfer_started=now();server->wire_in=server->waits=0;
            fprintf(psmtp_work_log(server->work),"stage=lan-cipher suite=0x%04x\n",(unsigned)session.cipher_suite);
            return MTP_OK;
        }
        (void)sceNetSocketClose(server->peer);server->peer=-1;
        continue;
reject:
        (void)sceNetSocketClose(server->peer);server->peer=-1;
        sceKernelUsleep(250000);
    }
    return MTP_CANCELLED;
}

int psmtp_lan_read(psmtp_lan_server *server,void *data,size_t size){
    server->until=now()+20000;
    return br_sslio_read_all(&server->io,data,size)?MTP_DISCONNECTED:MTP_OK;
}

int psmtp_lan_poll_read(psmtp_lan_server *server,void *data,size_t size,int *available){
    unsigned char byte;
    *available=0;
    if(!(br_ssl_engine_current_state(&server->tls.eng)&BR_SSL_RECVAPP)){
        int rc=sceNetRecv(server->peer,&byte,1,LAN_MSG_PEEK);
        if(retry(rc))return MTP_OK;
        if(rc<=0)return MTP_DISCONNECTED;
    }
    server->until=now()+1000;
    if(br_sslio_read_all(&server->io,data,size))return MTP_DISCONNECTED;
    *available=1;
    return MTP_OK;
}

int psmtp_lan_write(psmtp_lan_server *server,const void *data,size_t size){
    server->until=now()+20000;
    return br_sslio_write_all(&server->io,data,size) || br_sslio_flush(&server->io)?MTP_DISCONNECTED:MTP_OK;
}

int psmtp_lan_close(psmtp_lan_server *server){
    int rc=MTP_OK;
    if(!server)return rc;
    uint64_t ended=now();
    if(server->transfer_started && ended>=server->transfer_started)
        fprintf(psmtp_work_log(server->work),"stage=lan-traffic wire-bytes=%llu elapsed-ms=%llu socket-waits=%llu\n",
            (unsigned long long)server->wire_in,(unsigned long long)(ended-server->transfer_started),
            (unsigned long long)server->waits);
    if(server->peer>=0 && sceNetSocketClose(server->peer))rc=MTP_IO;
    if(server->listener>=0 && sceNetSocketClose(server->listener))rc=MTP_IO;
    if(server->pool>=0)sceNetPoolDestroy(server->pool);
    if(server->acquired)psmtp_lan_net_release(sceNetTerm);
    if(server->random_owned){
        int32_t status=sceSysmoduleUnloadModule(ORBIS_SYSMODULE_RANDOM);
        if(status){
            fprintf(psmtp_work_log(server->work),"stage=lan-random-unload native=0x%08x\n",(unsigned)status);
            if(!rc)rc=MTP_IO;
        }
    }
    free(server);
    return rc;
}
