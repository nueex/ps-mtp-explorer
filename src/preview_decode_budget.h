/* Per-operation decoder accounting. Returned stb buffers carry their owner,
 * so freeing them after the scope or on a different thread is safe. */
#ifndef PSMTP_PREVIEW_DECODE_BUDGET_H
#define PSMTP_PREVIEW_DECODE_BUDGET_H
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
    size_t live,peak,limit;
    unsigned refs,allocations,limited,oom;
    void *blocks[64];
} psmtp_decode_budget;
typedef union {
    struct {psmtp_decode_budget *owner;size_t charge;} value;
    uint64_t alignment[2];
} psmtp_decode_header;
typedef char psmtp_decode_header_is_16_bytes[sizeof(psmtp_decode_header)==16?1:-1];
#if defined(_MSC_VER) && !defined(__clang__)
#define PSMTP_DECODE_TLS __declspec(thread)
#else
#define PSMTP_DECODE_TLS __thread
#endif
static PSMTP_DECODE_TLS psmtp_decode_budget *psmtp_decode_current;
#ifdef PSMTP_TEST_DECODE
static PSMTP_DECODE_TLS unsigned psmtp_decode_fail_at=UINT32_MAX;
#endif

static void psmtp_decode_release(psmtp_decode_budget *b)
{
    if(b && __atomic_sub_fetch(&b->refs,1,__ATOMIC_ACQ_REL)==0)free(b);
}
static void *psmtp_decode_malloc(size_t size)
{
    psmtp_decode_budget *b=psmtp_decode_current;psmtp_decode_header *h;
    size_t charge,live,peak;unsigned slot;
    if(b && (__atomic_load_n(&b->limited,__ATOMIC_RELAXED) || __atomic_load_n(&b->oom,__ATOMIC_RELAXED)))return NULL;
#ifdef PSMTP_TEST_DECODE
    if(b && b->allocations==psmtp_decode_fail_at) {b->oom=1;return NULL;}
#endif
    if(!size)size=1;
    if(size>SIZE_MAX-sizeof(*h)) {
        if(b)__atomic_store_n(&b->limited,1,__ATOMIC_RELAXED);
        return NULL;
    }
    charge=size+sizeof(*h);
    if(b) {
        live=__atomic_load_n(&b->live,__ATOMIC_RELAXED);
        do {
            if(charge>b->limit || live>b->limit-charge) {
                __atomic_store_n(&b->limited,1,__ATOMIC_RELAXED);return NULL;
            }
        } while(!__atomic_compare_exchange_n(&b->live,&live,live+charge,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED));
        __atomic_add_fetch(&b->refs,1,__ATOMIC_RELAXED);
    }
    h=malloc(charge);
    if(!h) {
        if(b) {
            __atomic_sub_fetch(&b->live,charge,__ATOMIC_RELAXED);
            __atomic_store_n(&b->oom,1,__ATOMIC_RELAXED);psmtp_decode_release(b);
        }
        return NULL;
    }
    h->value.owner=b;h->value.charge=charge;
    if(b) {
        for(slot=0;slot<64;++slot) {
            void *empty=NULL;
            if(__atomic_compare_exchange_n(&b->blocks[slot],&empty,h,0,__ATOMIC_RELEASE,__ATOMIC_RELAXED))break;
        }
        if(slot==64) {
            free(h);__atomic_sub_fetch(&b->live,charge,__ATOMIC_RELAXED);
            __atomic_store_n(&b->limited,1,__ATOMIC_RELAXED);psmtp_decode_release(b);return NULL;
        }
        __atomic_add_fetch(&b->allocations,1,__ATOMIC_RELAXED);
        live=__atomic_load_n(&b->live,__ATOMIC_RELAXED);
        peak=__atomic_load_n(&b->peak,__ATOMIC_RELAXED);
        while(peak<live && !__atomic_compare_exchange_n(&b->peak,&peak,live,0,__ATOMIC_RELAXED,__ATOMIC_RELAXED)){}
    }
    return h+1;
}
static void psmtp_decode_free(void *p)
{
    psmtp_decode_header *h;psmtp_decode_budget *b;size_t charge;unsigned slot;
    if(!p)return;
    h=(psmtp_decode_header *)p-1;b=h->value.owner;charge=h->value.charge;
    if(b)for(slot=0;slot<64;++slot) {
        void *match=h;
        if(__atomic_compare_exchange_n(&b->blocks[slot],&match,NULL,0,__ATOMIC_ACQ_REL,__ATOMIC_RELAXED))break;
    }
    free(h);
    if(b) {__atomic_sub_fetch(&b->live,charge,__ATOMIC_ACQ_REL);psmtp_decode_release(b);}
}
/* Only at the exclusive end of a wrapper operation, before any stb pointer
 * escapes. Covers upstream failure paths that leave an intermediate buffer. */
static void psmtp_decode_cleanup(psmtp_decode_budget *b)
{
    unsigned slot;
    for(slot=0;slot<64;++slot) {
        psmtp_decode_header *h=__atomic_load_n(&b->blocks[slot],__ATOMIC_ACQUIRE);
        if(h)psmtp_decode_free(h+1);
    }
}
static void *psmtp_decode_realloc(void *p,size_t old_size,size_t size)
{
    psmtp_decode_header *h;psmtp_decode_budget *previous,*owner;void *next;size_t actual;
    if(!p)return psmtp_decode_malloc(size);
    if(!size) {psmtp_decode_free(p);return NULL;}
    h=(psmtp_decode_header *)p-1;owner=h->value.owner;
    actual=h->value.charge-sizeof(*h);(void)old_size;
    /* Account for both blocks at peak, including realloc growth and shrink.
     * On failure leave the old allocation and its accounting untouched. */
    previous=psmtp_decode_current;psmtp_decode_current=owner;
    next=psmtp_decode_malloc(size);psmtp_decode_current=previous;
    if(!next)return NULL;
    memcpy(next,p,actual<size?actual:size);psmtp_decode_free(p);return next;
}
#endif
