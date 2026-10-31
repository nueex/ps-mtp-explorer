#pragma once
#include <stdint.h>
#include "defs.h"

struct jbc_cred
{
    uid_t uid;
    uid_t ruid;
    uid_t svuid;
    gid_t rgid;
    gid_t svgid;
    uintptr_t prison;
    uintptr_t cdir;
    uintptr_t rdir;
    uintptr_t jdir;
    uint64_t sceProcType;
    uint64_t sonyCred;
    uint64_t sceProcCap;
};


uintptr_t jbc_get_prison0(void);
uintptr_t jbc_get_rootvnode(void);
int jbc_get_cred(struct jbc_cred*);
int jbc_jailbreak_cred(struct jbc_cred*);
int jbc_set_cred(const struct jbc_cred*);

/* Main-thread only, before workers start / after all users have drained.
 * The saved root pointers stay alive through three OS descriptor references. */
struct jbc_context { struct jbc_cred cred; int fds[3]; int cred_fd; int held; };
/* -2: same-UID private credential copy unavailable; no raw prison hold. */
int jbc_context_save(struct jbc_context*);
int jbc_context_restore(struct jbc_context*);

/* User-space diagnostic snapshot; no kernel addresses or extra kernel writes. */
struct jbc_diagnostic { int stage,native_error; uint32_t old_refs,target_refs; };
void jbc_get_diagnostic(struct jbc_diagnostic*);
