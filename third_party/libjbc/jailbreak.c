/* PSMTP integration changes are documented in NOTICE.txt. */
#include "defs.h"
#include "kernelrw.h"
#include "jailbreak.h"

#ifndef PSMTP_TEST_JBC
__asm__("psmtp_jbc_open:\nmov $5, %rax\nmov %rcx, %r10\nsyscall\njnc 1f\nneg %rax\n1: ret");
#ifdef PSMTP_EXPERIMENTAL_CREDENTIAL_SNAPSHOT
__asm__("psmtp_jbc_setuid:\nmov $23, %rax\nmov %rcx, %r10\nsyscall\njnc 1f\nneg %rax\n1: ret");
#endif
__asm__("psmtp_jbc_close:\nmov $6, %rax\nmov %rcx, %r10\nsyscall\njnc 1f\nneg %rax\n1: ret");

#endif
int psmtp_jbc_open(const char*, int);
int psmtp_jbc_close(int);
int psmtp_jbc_setuid(uid_t);

static struct jbc_diagnostic diagnostic;
void jbc_get_diagnostic(struct jbc_diagnostic* out){*out=diagnostic;}
static uintptr_t prison0;
static uintptr_t rootvnode;

static int resolve(void)
{
unsigned restarts=0, visited;
restart:;
    if(++restarts>8) return -1;
    visited=0;
    uintptr_t td = jbc_krw_get_td();
    if(!td) return -1;
    uintptr_t proc = jbc_krw_read64(td+8, KERNEL_HEAP);
    for(;;)
    {
        if(++visited>4096) return -1;
        int pid;
        if(jbc_krw_memcpy((uintptr_t)&pid, proc+0xb0, sizeof(pid), KERNEL_HEAP))
            goto restart;
        if(pid == 1)
            break;
        uintptr_t proc2 = jbc_krw_read64(proc, KERNEL_HEAP);
        uintptr_t proc1 = jbc_krw_read64(proc2+8, KERNEL_HEAP);
        if(proc1 != proc)
            goto restart;
        proc = proc2;
    }
    uintptr_t pid1_ucred = jbc_krw_read64(proc+0x40, KERNEL_HEAP);
    uintptr_t pid1_fd = jbc_krw_read64(proc+0x48, KERNEL_HEAP);
    if(jbc_krw_memcpy((uintptr_t)&prison0, pid1_ucred+0x30, sizeof(prison0), KERNEL_HEAP))
        return -1;
    if(jbc_krw_memcpy((uintptr_t)&rootvnode, pid1_fd+0x18, sizeof(rootvnode), KERNEL_HEAP))
    {
        prison0 = 0;
        return -1;
    }
    return 0;
}

uintptr_t jbc_get_prison0(void)
{
    if(!prison0)
        resolve();
    return prison0;
}

uintptr_t jbc_get_rootvnode(void)
{
    if(!rootvnode)
        resolve();
    return rootvnode;
}

static inline int ppcopyout(void* u1, void* u2, uintptr_t k)
{
    return jbc_krw_memcpy((uintptr_t)u1, k, (uintptr_t)u2-(uintptr_t)u1, KERNEL_HEAP);
}

static inline int ppcopyin(const void* u1, const void* u2, uintptr_t k)
{
    return jbc_krw_memcpy(k, (uintptr_t)u1, (uintptr_t)u2-(uintptr_t)u1, KERNEL_HEAP);
}

int jbc_get_cred(struct jbc_cred* ans)
{
    uintptr_t td = jbc_krw_get_td();
    if(!td) return -1;
    uintptr_t proc = jbc_krw_read64(td + 8, KERNEL_HEAP);
    uintptr_t ucred = jbc_krw_read64(proc + 0x40, KERNEL_HEAP);
    uintptr_t fd = jbc_krw_read64(proc + 0x48, KERNEL_HEAP);

    if (ppcopyout(&ans->uid, 1 + &ans->svuid, ucred + 4)
        || ppcopyout(&ans->rgid, 1 + &ans->svgid, ucred + 20)
        || ppcopyout(&ans->prison, 1 + &ans->prison, ucred + 0x30)
        || ppcopyout(&ans->cdir, 1 + &ans->jdir, fd + 0x10)
        || ppcopyout(&ans->sceProcType, 1 + &ans->sceProcCap, ucred + 88))
        return -1;

    return 0;
}

static int jbc_set_cred_internal(const struct jbc_cred* ans)
{
    uintptr_t td = jbc_krw_get_td();
    if(!td) return -1;
    uintptr_t proc = jbc_krw_read64(td + 8, KERNEL_HEAP);
    uintptr_t ucred = jbc_krw_read64(proc + 0x40, KERNEL_HEAP);
    uintptr_t fd = jbc_krw_read64(proc + 0x48, KERNEL_HEAP);

    if (ppcopyin(&ans->uid, 1 + &ans->svuid, ucred + 4)
        || ppcopyin(&ans->rgid, 1 + &ans->svgid, ucred + 20)
        || ppcopyin(&ans->prison, 1 + &ans->prison, ucred + 0x30)
        || ppcopyin(&ans->cdir, 1 + &ans->jdir, fd + 0x10) 
        || ppcopyin(&ans->sceProcType, 1 + &ans->sceProcCap, ucred + 88))
        return -1;
    return 0;
}

int jbc_jailbreak_cred(struct jbc_cred* ans)
{
    uintptr_t prison0 = jbc_get_prison0();
    if (!prison0)
        return -1;
    uintptr_t rootvnode = jbc_get_rootvnode();
    if (!rootvnode)
        return -1;

    //without some modules wont load like Apputils
    ans->sceProcCap = 0xffffffffffffffff;
    ans->sceProcType = 0x3801000000000013;
    ans->sonyCred = 0xffffffffffffffff;

    ans->uid = ans->ruid = ans->svuid = ans->rgid = ans->svgid = 0;
    ans->prison = prison0;
    ans->cdir = ans->rdir = ans->jdir = rootvnode;
    return 0;
}

static int jbc_open_this(const struct jbc_cred* cred0, uintptr_t vnode)
{
    if(!vnode)
        return -1;
    struct jbc_cred cred = *cred0;
    if(jbc_jailbreak_cred(&cred)) return -1;
    cred.cdir = cred.rdir = cred.jdir = vnode;
    if(jbc_set_cred_internal(&cred)) {(void)jbc_set_cred_internal(cred0);return -1;}
    int ans = psmtp_jbc_open("/", 0);
    if(jbc_set_cred_internal(cred0)) {if(ans>=0) psmtp_jbc_close(ans);return -1;}
    return ans;
}

static int swap64(uintptr_t p1, KmemKind k1, uintptr_t p2, KmemKind k2)
{
    uintptr_t v1 = jbc_krw_read64(p1, k1);
    uintptr_t v2 = jbc_krw_read64(p2, k2);
    if(ppcopyin(&v2, 1+&v2, p1)) return -1;
    if(ppcopyin(&v1, 1+&v1, p2)) {
        /* Do not leave two owners of v2 when the second write fails. */
        (void)ppcopyin(&v1, 1+&v1, p1);return -1;
    }
    return 0;
}

/* Verify the descriptor indirection actually used by the vnode swap against
 * a descriptor opened by the OS, before any credential writes. Canonical
 * pointers alone do not establish a compatible filedesc/file layout. */
static int context_preflight(const struct jbc_cred* old, const struct jbc_cred* next)
{
    uintptr_t td = jbc_krw_get_td();
    if(!td) return -1;
    uintptr_t proc = jbc_krw_read64(td+8, KERNEL_HEAP);
    uintptr_t fd = jbc_krw_read64(proc+0x48, KERNEL_HEAP);
    uintptr_t files = jbc_krw_read64(fd, KERNEL_HEAP);
    if(files<0xffff800000000000 || files>=0xffffffff00000000 || (files&7)) return -1;
    uintptr_t prisons[2] = {old->prison, next->prison};
    for(unsigned i=0;i<2;++i) {
        uint32_t refs=0;
        if(!prisons[i] || (prisons[i]&7)) return -1;
        if(jbc_krw_memcpy((uintptr_t)&refs,prisons[i]+0x14,sizeof(refs),KERNEL_HEAP)
        && jbc_krw_memcpy((uintptr_t)&refs,prisons[i]+0x14,sizeof(refs),KERNEL_TEXT)) return -1;
        if(!refs || refs==UINT32_MAX) return -1;
    }
    int probe=psmtp_jbc_open("/",0);
    if(probe<0) {diagnostic.native_error=probe;return -1;}
    uintptr_t file=jbc_krw_read64(files+(uintptr_t)probe*8,KERNEL_HEAP);
    int valid=file>=0xffff800000000000 && file<0xffffffff00000000 && !(file&7)
        && jbc_krw_read64(file,KERNEL_HEAP)==old->rdir;
    if(psmtp_jbc_close(probe)) valid=0;
    return valid?0:-1;
}

/* Each root owns one vnode reference, even when all three point at the
 * same vnode. Transfer those references through OS-opened descriptors; their
 * close performs the kernel vrele. Never edit a guessed vnode refcount. */
#if defined(PSMTP_TEST_JBC) && defined(PSMTP_EXPERIMENTAL_CREDENTIAL_SNAPSHOT)
static uint32_t* test_prison_ref_address(uintptr_t);
#endif
#ifdef PSMTP_EXPERIMENTAL_CREDENTIAL_SNAPSHOT
/* Execute the read/modify/write inside one HEN gate. Prison references are
 * shared with other processes, so separate read/write syscalls lose updates. */
static uint64_t k_prison_ref(uint64_t prison,uint64_t add,uint64_t c,uint64_t d,uint64_t e,uint64_t f)
{
    uint32_t *counter,refs,next;
    if(prison<0xffff800000000000 || prison>=0xfffffffffffff000 || (prison&7)) return UINT64_MAX;
#ifdef PSMTP_TEST_JBC
    counter=test_prison_ref_address(prison+0x14);
#else
    counter=(uint32_t *)(uintptr_t)(prison+0x14);
#endif
    if(!counter)return UINT64_MAX;
    refs=__atomic_load_n(counter,__ATOMIC_ACQUIRE);
    for(unsigned tries=0;tries<128;++tries){
        if(!refs || (add && refs==UINT32_MAX) || (!add && refs==1))return UINT64_MAX;
        next=add?refs+1:refs-1;
        if(__atomic_compare_exchange_n(counter,&refs,next,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return 0;
    }
    return UINT64_MAX;
}
static int prison_ref(uintptr_t prison,int delta)
{
    return jbc_krw_kcall((uintptr_t)k_prison_ref,prison,delta>0,0,0,0,0)==0?0:-1;
}
#else
/* Keep the previously console-tested libjbc kernel-copy ABI in the default
 * profile. Do not run a new arbitrary C kernel callback during startup. */
static int prison_ref(uintptr_t prison,int delta){
    uint32_t refs=0;
    if(jbc_krw_memcpy((uintptr_t)&refs,prison+0x14,sizeof(refs),KERNEL_HEAP)
    && jbc_krw_memcpy((uintptr_t)&refs,prison+0x14,sizeof(refs),KERNEL_TEXT))return -1;
    if((delta>0 && refs==UINT32_MAX) || (delta<0 && !refs))return -1;
    refs=delta>0?refs+1:refs-1;
    if(jbc_krw_memcpy(prison+0x14,(uintptr_t)&refs,sizeof(refs),KERNEL_HEAP)
    && jbc_krw_memcpy(prison+0x14,(uintptr_t)&refs,sizeof(refs),KERNEL_TEXT))return -1;
    return 0;
}
#endif
static uint32_t prison_refs(uintptr_t prison){
    uint32_t refs=UINT32_MAX;
    if(prison>=0xffff800000000000 && prison<0xfffffffffffff000 && !(prison&7)){
        if(jbc_krw_memcpy((uintptr_t)&refs,prison+0x14,sizeof(refs),KERNEL_HEAP))
            (void)jbc_krw_memcpy((uintptr_t)&refs,prison+0x14,sizeof(refs),KERNEL_TEXT);
    }
    return refs;
}
int jbc_set_cred(const struct jbc_cred* newp)
{
    struct jbc_cred old,neww=*newp,elevated;
    int fds[3]={-1,-1,-1},result=-1,swapped=0,added=0,removed=0,changed=0;
    uintptr_t files[3]={0},td,proc,fd,ofiles,roots[3];
    diagnostic=(struct jbc_diagnostic){1,0,0,0};
    if(jbc_get_cred(&old)) return -1;
    diagnostic.old_refs=prison_refs(old.prison);diagnostic.target_refs=prison_refs(neww.prison);diagnostic.stage=2;
    /* This application requires real cdir/rdir/jdir references. Reject a
     * nullable namespace instead of installing a userspace fake file vtable. */
    if(!old.cdir || !old.rdir || !old.jdir) return -1;
    if(!neww.cdir) neww.cdir=jbc_get_rootvnode();
    if(!neww.rdir) neww.rdir=jbc_get_rootvnode();
    if(!neww.jdir) neww.jdir=jbc_get_rootvnode();
    diagnostic.stage=3;
    if(context_preflight(&old,&neww)) return -1;
    roots[0]=neww.cdir;roots[1]=neww.rdir;roots[2]=neww.jdir;
    for(int i=0;i<3;++i) {
        diagnostic.stage=10+i;fds[i]=jbc_open_this(&old,roots[i]);if(fds[i]<0){diagnostic.native_error=fds[i];goto cleanup;}
    }
    diagnostic.stage=20;td=jbc_krw_get_td();if(!td) goto cleanup;
    proc=jbc_krw_read64(td+8,KERNEL_HEAP);fd=jbc_krw_read64(proc+0x48,KERNEL_HEAP);
    ofiles=jbc_krw_read64(fd,KERNEL_HEAP);
    for(int i=0;i<3;++i) {
        diagnostic.stage=21+i;files[i]=jbc_krw_read64(ofiles+8*fds[i],KERNEL_HEAP);
        if(files[i]<0xffff800000000000 || files[i]>=0xffffffff00000000 || (files[i]&7)
        || jbc_krw_read64(files[i],KERNEL_HEAP)!=roots[i]) goto cleanup;
    }
    elevated=neww;elevated.cdir=old.cdir;elevated.rdir=old.rdir;elevated.jdir=old.jdir;
    if(old.prison!=neww.prison) {
        diagnostic.stage=30;diagnostic.target_refs=prison_refs(neww.prison);
        if(prison_ref(neww.prison,1)) goto cleanup;added=1;
    }
    changed=1;diagnostic.stage=31;
    if(jbc_set_cred_internal(&elevated)) goto rollback;
    if(added) {diagnostic.stage=32;diagnostic.old_refs=prison_refs(old.prison);if(prison_ref(old.prison,-1)) goto rollback;removed=1;}
    for(int i=0;i<3;++i) {
        diagnostic.stage=40+i;if(swap64(files[i],KERNEL_HEAP,fd+0x10+8*i,KERNEL_HEAP)) goto rollback;
        ++swapped;
    }
    result=0;goto cleanup;
rollback:
    while(swapped) {--swapped;if(swap64(files[swapped],KERNEL_HEAP,fd+0x10+8*swapped,KERNEL_HEAP)) return -1;}
    if(removed && prison_ref(old.prison,1)) return -1;
    if(changed && jbc_set_cred_internal(&old)) return -1;
    if(added && prison_ref(neww.prison,-1)) return -1;
cleanup:
    for(int i=0;i<3;++i) if(fds[i]>=0 && psmtp_jbc_close(fds[i])) {diagnostic.stage=50+i;result=-1;}
    if(!result)diagnostic.stage=0;
    return result;
}

#ifdef PSMTP_EXPERIMENTAL_CREDENTIAL_SNAPSHOT
int jbc_context_save(struct jbc_context* ctx)
{
    uintptr_t roots[3],td,proc,original_cred,private_cred,fd,table,file;struct jbc_cred after;int result=-1;
    ctx->held=0;ctx->cred_fd=-1;for(int i=0;i<3;++i)ctx->fds[i]=-1;
    diagnostic=(struct jbc_diagnostic){60,0,0,0};
    if(jbc_get_cred(&ctx->cred) || context_preflight(&ctx->cred,&ctx->cred)) return -1;
    td=jbc_krw_get_td();if(!td)return -1;proc=jbc_krw_read64(td+8,KERNEL_HEAP);
    original_cred=jbc_krw_read64(proc+0x40,KERNEL_HEAP);
    diagnostic.stage=61;diagnostic.old_refs=prison_refs(ctx->cred.prison);
    /* Open the credential anchor BEFORE any temporary elevation. A root
     * opened by jbc_open_this can capture GLOBAL file credentials even if
     * its vnode is the old sandbox root. Such a file cannot pin the old jail. */
    ctx->cred_fd=psmtp_jbc_open("/",0);
    if(ctx->cred_fd<0){diagnostic.native_error=ctx->cred_fd;goto fail;}
    fd=jbc_krw_read64(proc+0x48,KERNEL_HEAP);table=jbc_krw_read64(fd,KERNEL_HEAP);
    file=jbc_krw_read64(table+8*ctx->cred_fd,KERNEL_HEAP);
    if(file<0xffff800000000000 || file>=0xffffffff00000000 || (file&7) || jbc_krw_read64(file,KERNEL_HEAP)!=ctx->cred.rdir)goto fail;
    roots[0]=ctx->cred.cdir;roots[1]=ctx->cred.rdir;roots[2]=ctx->cred.jdir;
    for(int i=0;i<3;++i) {
        diagnostic.stage=62+i;ctx->fds[i]=jbc_open_this(&ctx->cred,roots[i]);if(ctx->fds[i]<0){diagnostic.native_error=ctx->fds[i];goto fail;}
    }
    /* The descriptors' f_cred holds the original ucred. A same-UID kernel
     * credential copy makes that hold independent of the elevated process.
     * The kernel now owns the prison hold and reaps it even after SIGKILL.
     * A raw +1 prison pin would be orphaned when no cleanup callback runs. */
    diagnostic.stage=65;
    {int rc=psmtp_jbc_setuid(ctx->cred.uid);if(rc){diagnostic.native_error=rc;result=-2;goto fail;}}
    private_cred=jbc_krw_read64(proc+0x40,KERNEL_HEAP);
    if(private_cred==original_cred){result=-2;goto fail;}
    if(private_cred<0xffff800000000000 || private_cred>=0xffffffff00000000 || (private_cred&7))goto fail;
    if(jbc_get_cred(&after) || after.prison!=ctx->cred.prison || after.cdir!=ctx->cred.cdir || after.rdir!=ctx->cred.rdir || after.jdir!=ctx->cred.jdir)goto fail;
    /* Preserve all original IDs/Sony fields if setuid normalizes saved IDs. */
    diagnostic.stage=66;if(jbc_set_cred_internal(&ctx->cred))goto fail;
    diagnostic.target_refs=prison_refs(ctx->cred.prison);diagnostic.stage=0;ctx->held=1;return 0;
fail:
    for(int i=0;i<3;++i)if(ctx->fds[i]>=0){psmtp_jbc_close(ctx->fds[i]);ctx->fds[i]=-1;}
    if(ctx->cred_fd>=0){psmtp_jbc_close(ctx->cred_fd);ctx->cred_fd=-1;}
    return result;
}
int jbc_context_restore(struct jbc_context* ctx)
{
    struct jbc_cred after;int rc=0;
    if(!ctx->held) return 0;
    if(jbc_set_cred(&ctx->cred) || jbc_get_cred(&after)) return -1;
    if(after.uid!=ctx->cred.uid || after.ruid!=ctx->cred.ruid || after.svuid!=ctx->cred.svuid
    || after.rgid!=ctx->cred.rgid || after.svgid!=ctx->cred.svgid || after.prison!=ctx->cred.prison
    || after.cdir!=ctx->cred.cdir || after.rdir!=ctx->cred.rdir || after.jdir!=ctx->cred.jdir
    || after.sceProcType!=ctx->cred.sceProcType || after.sonyCred!=ctx->cred.sonyCred
    || after.sceProcCap!=ctx->cred.sceProcCap) return -1;
    /* Only release the saved pins after every process field is verified. */
    /* Closing the descriptor-owned ucred performs the kernel prison_free. */
    ctx->held=0;
    for(int i=0;i<3;++i)if(ctx->fds[i]>=0){if(psmtp_jbc_close(ctx->fds[i]))rc=-1;ctx->fds[i]=-1;}
    if(ctx->cred_fd>=0){if(psmtp_jbc_close(ctx->cred_fd))rc=-1;ctx->cred_fd=-1;}
    return rc;
}

#else
int jbc_context_save(struct jbc_context* ctx){
    /* The console log shows the credential-copy build fails before browsing.
     * No copied credentials, saved vnode descriptors or unowned prison pins
     * are introduced in the default, console-tested access profile. */
    ctx->held=0;ctx->cred_fd=-1;for(int i=0;i<3;++i)ctx->fds[i]=-1;
    diagnostic=(struct jbc_diagnostic){80,0,0,0};return -2;
}
int jbc_context_restore(struct jbc_context* ctx){return ctx->held?-1:0;}
#endif
