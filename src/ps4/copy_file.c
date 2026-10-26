/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */




#define _GNU_SOURCE
#include "copy_file.h"
#include "storage_space.h"
#include "stat_abi.h"
#include "psmtp/ui_format.h"
#include <errno.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <stdlib.h>
#ifdef PSMTP_TEST_FILE_SYSTEM
#include "ps4_copy_test_api.h"
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
typedef char offset_is_64[(sizeof(off_t)==8)?1:-1];
#endif
static int failure(psmtp_copy_file *f,const char *stage,int64_t rc) {
    int error=errno;
    if(!f->error_stage[0]) { snprintf(f->error_stage,sizeof(f->error_stage),"%s",stage); f->error_number=error; }
    fprintf(f->log,"stage=copy-fs-%s rc=%" PRId64 " errno=%d\n",stage,rc,error); return error==ENOSPC?MTP_NO_SPACE:MTP_FILESYSTEM;
}
/* The SDK typedef has a 32-bit mode_t, shifting size/blocks by eight bytes.
 * Pass native storage through void*; never decode the SDK struct stat. */
static int status(psmtp_copy_file *f,int fd,const char *path,psmtp_native_stat *out) {
    union { psmtp_native_stat stat; uint8_t backing[512]; } data; int rc,e=0;
    if(path) {
        /* lstat resolves on PS4 but returns EPERM. Open without following the
         * final component, then inspect the descriptor, as for the pinned root. */
        fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
        if(fd<0) { fprintf(f->log,"stage=copy-fs-path-check path=%s\n",path); return failure(f,"path-open",fd); }
    }
    memset(&data,0,sizeof(data)); rc=fstat(fd,(void *)&data.stat);
    if(rc) { fprintf(f->log,"stage=copy-fs-descriptor-query fd=%d path=%s\n",fd,path?path:"(pinned descriptor)"); e=failure(f,"fstat",rc); }
    else *out=data.stat;
    if(path) { rc=close(fd); if(rc && !e) e=failure(f,"path-close",rc); }
    return e;
}
static int identity(psmtp_copy_file *f,const char *path,uint64_t dev,uint64_t ino,int directory) {
    psmtp_native_stat s; int e=status(f,-1,path,&s); if(e) return e;
    if((directory?!S_ISDIR(s.st_mode):!S_ISREG(s.st_mode)) || (uint64_t)s.st_dev!=dev || (uint64_t)s.st_ino!=ino) {
        fprintf(f->log,"stage=copy-fs-identity observed-mode=%o dev=%" PRIu64 " ino=%" PRIu64 " expected-dev=%" PRIu64 " ino=%" PRIu64 "\n",
            (unsigned)s.st_mode,(uint64_t)s.st_dev,(uint64_t)s.st_ino,dev,ino);
        return failure(f,"identity-changed",-1);
    }
    if(directory && path==f->directory_path &&
       (f->directory_readable ? (s.st_mode&0777)!=0755 : (s.st_mode&0077)!=0)) return failure(f,"directory-permissions",-1);
    return 0;
}
static int copy_path(char *out,size_t cap,const char *parent,const char *name) {
    size_t a=strlen(parent),b=strlen(name);
    if(a+1+b>=cap) return MTP_LIMIT;
    memcpy(out,parent,a); out[a]='/'; memcpy(out+a+1,name,b+1); return 0;
}
static int parent_check(psmtp_copy_file *f) {
    char walk[256]; size_t i,n=strlen(f->parent_path),base=strlen(PSMTP_COPY_DIRECTORY); psmtp_native_stat st;
    if(!n) return 0;
    if(n>=sizeof(walk) || strncmp(f->parent_path,PSMTP_COPY_DIRECTORY,base) || f->parent_path[base]!='/') return MTP_STATE;
    memcpy(walk,f->parent_path,n+1);
    for(i=base+1;i<=n;++i) if(walk[i]=='/' || !walk[i]) {
        char save=walk[i]; int e;
        walk[i]=0; e=status(f,-1,walk,&st); walk[i]=save;
        if(e) return e;
        if(!S_ISDIR(st.st_mode)) return failure(f,"parent-type",-1);
    }
    return f->parent>=0?identity(f,f->parent_path,f->parent_dev,f->parent_ino,1):0;
}
static int chain(psmtp_copy_file *f) {
    int e=identity(f,"/data",f->data_dev,f->data_ino,1); if(e) return e;
    e=identity(f,PSMTP_LOG_DIRECTORY,f->base_dev,f->base_ino,1); if(e) return e;
    e=identity(f,PSMTP_COPY_DIRECTORY,f->root_dev,f->root_ino,1); if(e) return e;
    e=parent_check(f); if(e) return e;
    return f->owned_directory?identity(f,f->directory_path,f->dir_dev,f->dir_ino,1):0;
}
static int begin(void *v,const mtp_copy_source *s) {
    psmtp_copy_file *f=v; unsigned attempt; uint64_t stamp=(uint64_t)time(NULL); int rc,e; psmtp_native_stat st;
    e=psmtp_space_require("/",PSMTP_COPY_DIRECTORY,s->size,f->log);if(e)return e;
    f->expected_size=s->size;
    fputs("stage=copy-fs-stat-abi bytes=120 mode-offset=8 mode-bytes=2 size-offset=72 blocks-offset=80\n",f->log);
    fputs("stage=copy-fs-method private-directory absolute-open descriptor-checks; no openat/lstat/hard-link calls\n",f->log);
    f->data=open("/data",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(f->data<0) return failure(f,"open-data",f->data);
    e=status(f,f->data,NULL,&st); if(e) return e; f->data_dev=st.st_dev; f->data_ino=st.st_ino;
    e=identity(f,"/data",f->data_dev,f->data_ino,1); if(e) return e;
    rc=mkdir(PSMTP_LOG_DIRECTORY,0755); if(rc && errno!=EEXIST) return failure(f,"mkdir-base",rc);
    f->base=open(PSMTP_LOG_DIRECTORY,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(f->base<0) return failure(f,"open-base",f->base);
    e=status(f,f->base,NULL,&st); if(e) return e; f->base_dev=st.st_dev; f->base_ino=st.st_ino;
    e=identity(f,PSMTP_LOG_DIRECTORY,f->base_dev,f->base_ino,1); if(e) return e;
    rc=mkdir(PSMTP_COPY_DIRECTORY,0755); if(rc && errno!=EEXIST) return failure(f,"mkdir",rc);
    f->root=open(PSMTP_COPY_DIRECTORY,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(f->root<0) return failure(f,"open-directory",f->root);
    fputs("stage=copy-fs-directory-open path=" PSMTP_COPY_DIRECTORY " method=absolute-nofollow\n",f->log);
    e=status(f,f->root,NULL,&st); if(e) return e; f->root_dev=st.st_dev; f->root_ino=st.st_ino;
    if(f->parent_path[0]) {
        e=parent_check(f); if(e) return e;
        f->parent=open(f->parent_path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        if(f->parent<0) return failure(f,"open-copy-parent",f->parent);
        e=status(f,f->parent,NULL,&st); if(e) return e; f->parent_dev=st.st_dev; f->parent_ino=st.st_ino;
    }
    for(attempt=0;attempt<128;++attempt) {
        e=chain(f); if(e) return e;
        snprintf(f->directory_name,sizeof(f->directory_name),"psmtp-%" PRIu64 "-%08" PRIx32 "-%u",stamp,s->handle,attempt);
        e=copy_path(f->directory_path,sizeof(f->directory_path),f->parent_path[0]?f->parent_path:PSMTP_COPY_DIRECTORY,f->directory_name); if(e) return e;
        rc=mkdir(f->directory_path,0700);
        if(rc) { if(errno==EEXIST) continue; return failure(f,"reserve-directory",rc); }
        f->owned_directory=1;
        e=status(f,-1,f->directory_path,&st); if(e) return e; f->dir_dev=st.st_dev; f->dir_ino=st.st_ino;
        f->directory=open(f->directory_path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        if(f->directory<0) return failure(f,"open-private-directory",f->directory);
        e=status(f,f->directory,NULL,&st); if(e) return e;
        if((uint64_t)st.st_dev!=f->dir_dev || (uint64_t)st.st_ino!=f->dir_ino) return failure(f,"directory-replaced",-1);
        e=chain(f); if(e) return e;
        e=copy_path(f->temporary,sizeof(f->temporary),f->directory_path,"transfer.part"); if(e) return e;
        {
            char extension[12],leaf[20];psmtp_ui_extension(extension,s->info.name,s->is_pkg);
            snprintf(leaf,sizeof(leaf),"file.%s",extension);
            e=copy_path(f->path,sizeof(f->path),f->directory_path,leaf); if(e) return e;
        }
        f->file=open(f->temporary,O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
        if(f->file<0) return failure(f,"create",f->file);
        f->owned=1; e=status(f,f->file,NULL,&st); if(e) return e;
        f->file_dev=st.st_dev; f->file_ino=st.st_ino;
        e=identity(f,f->temporary,f->file_dev,f->file_ino,0); if(e) return e;
        fprintf(f->log,"stage=copy-fs-created target=%s exclusive-directory=1 exclusive-file=1\n",f->path); return 0;
    }
    return failure(f,"name-limit",-1);
}
static int put(void *v,const uint8_t *p,size_t n,size_t *actual) {
    psmtp_copy_file *f=v; ssize_t rc;
    do { rc=write(f->file,p,n); } while(rc<0 && errno==EINTR);
    *actual=rc<0?0:(size_t)rc; return rc<0?failure(f,"write",rc):0;
}
static int sync_file(void *v) { psmtp_copy_file *f=v; int rc=fsync(f->file); return rc?failure(f,"sync-file",rc):0; }
static int rewind_file(void *v) { psmtp_copy_file *f=v; int64_t rc=lseek(f->file,0,SEEK_SET); return rc?failure(f,"rewind",rc):0; }
static int get(void *v,uint8_t *p,size_t n,size_t *actual) {
    psmtp_copy_file *f=v; ssize_t rc;
    do { rc=read(f->file,p,n); } while(rc<0 && errno==EINTR);
    *actual=rc<0?0:(size_t)rc; return rc<0?failure(f,"read-back",rc):0;
}
static int space(void *v,uint64_t *available) {
    psmtp_copy_file *f=v;psmtp_native_stat st;int e;*available=0;
    int fds[5]={f->data,f->base,f->root,f->parent,f->directory};
    uint64_t devs[5]={f->data_dev,f->base_dev,f->root_dev,f->parent_dev,f->dir_dev};
    uint64_t inos[5]={f->data_ino,f->base_ino,f->root_ino,f->parent_ino,f->dir_ino};size_t i;


    for(i=0;i<5;++i)if(fds[i]>=0){e=status(f,fds[i],NULL,&st);if(e)return e;if(!S_ISDIR(st.st_mode) || (uint64_t)st.st_dev!=devs[i] || (uint64_t)st.st_ino!=inos[i])return MTP_VERIFY;
        if(i==4 && (f->directory_readable?(st.st_mode&0777)!=0755:(st.st_mode&0077)!=0))return MTP_VERIFY;}
    e=status(f,f->file,NULL,&st);if(e)return e;
    if(!S_ISREG(st.st_mode) || (uint64_t)st.st_dev!=f->file_dev || (uint64_t)st.st_ino!=f->file_ino || st.st_size<0 || (uint64_t)st.st_size>f->expected_size)return MTP_VERIFY;
    return MTP_COPY_SPACE_STREAMING;
}

static int readable(psmtp_copy_file *f,int fd,unsigned mode,int directory) {
    psmtp_native_stat st; int rc=fchmod(fd,mode),e;
    if(rc) return failure(f,directory?"share-directory":"share-file",rc);
    e=status(f,fd,NULL,&st); if(e) return e;
    if((directory?!S_ISDIR(st.st_mode):!S_ISREG(st.st_mode)) ||
       (uint64_t)st.st_dev!=(directory?f->dir_dev:f->file_dev) ||
       (uint64_t)st.st_ino!=(directory?f->dir_ino:f->file_ino) || (st.st_mode&0777)!=mode) {
        errno=EIO; return failure(f,"share-permissions-check",-1);
    }
    fprintf(f->log,"stage=copy-access object=%s mode=%04o uid=%" PRIu32 " gid=%" PRIu32 "\n",
        directory?"directory":"file",(unsigned)(st.st_mode&0777),st.st_uid,st.st_gid);
    return 0;
}
static int publish(void *v,int *published) {
    psmtp_copy_file *f=v; uint64_t ignored; psmtp_native_stat st; int target,rc,e=space(f,&ignored);
    if(e!=MTP_COPY_SPACE_STREAMING) return e?e:MTP_STATE;
    e=status(f,f->file,NULL,&st);if(e)return e;
    if(st.st_size<0 || (uint64_t)st.st_size!=f->expected_size)return MTP_VERIFY;
    e=chain(f); if(e) return e;
    e=identity(f,f->temporary,f->file_dev,f->file_ino,0); if(e) return e;


    errno=0; target=open(f->path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    if(target>=0) { e=failure(f,"unexpected-target",target); (void)close(target); return e; }
    if(errno!=ENOENT) return failure(f,"target-check",target);

    e=readable(f,f->file,0644,0); if(e) return e;
    rc=fsync(f->file); if(rc) return failure(f,"sync-file-permissions",rc);
    rc=close(f->file); f->file=-1; if(rc) return failure(f,"close-file",rc);
    rc=rename(f->temporary,f->path); if(rc) return failure(f,"publish-private",rc);
    *published=f->published=1; f->owned=0;
    fprintf(f->log,"stage=copy-published target=%s method=private-directory-rename; installation-not-started\n",f->path);
    e=identity(f,f->path,f->file_dev,f->file_ino,0); if(e) return e;
    rc=fsync(f->directory); if(rc) return failure(f,"sync-private-directory",rc);
    if(f->parent>=0) { rc=fsync(f->parent); if(rc) return failure(f,"sync-copy-parent",rc); }
    rc=fsync(f->root); return rc?failure(f,"sync-directory",rc):0;
}
static int dispose(void *v) {
    psmtp_copy_file *f=v; int error=0,rc,e;
    if(f->file>=0) { rc=close(f->file); if(rc) error=failure(f,"close-temp",rc); f->file=-1; }
    if(f->owned) {
        e=chain(f); if(!e) e=identity(f,f->temporary,f->file_dev,f->file_ino,0);
        if(e) error=e;
        else { rc=unlink(f->temporary); if(rc) error=failure(f,"remove-temp",rc); else f->owned=0; }
    }
    if(f->owned_directory && !f->published && !f->owned) {
        e=chain(f); if(e) error=e;
        else { rc=rmdir(f->directory_path); if(rc) error=failure(f,"remove-empty-directory",rc); else f->owned_directory=0; }
    }


    if(!error && f->published && !f->owned) {
        e=chain(f); if(!e) e=identity(f,f->path,f->file_dev,f->file_ino,0);
        if(!e) e=readable(f,f->directory,0755,1);
        if(e) error=e;
        else {
            f->directory_readable=1;
            e=chain(f); if(e) error=e;
            if(!error) { rc=fsync(f->directory); if(rc) error=failure(f,"sync-readable-directory",rc); }
            if(!error) { rc=fsync(f->root); if(rc) error=failure(f,"sync-readable-root",rc); }
            if(!error) fprintf(f->log,"stage=copy-access-ready target=%s directory-mode=0755 file-mode=0644\n",f->path);
        }
    }
    if(f->root>=0 && f->owned_directory==0) { rc=fsync(f->root); if(rc) error=failure(f,"sync-cleanup",rc); }
    if(f->directory>=0) { rc=close(f->directory); if(rc) error=failure(f,"close-private-directory",rc); f->directory=-1; }
    if(f->parent>=0) { rc=fsync(f->parent); if(rc) error=failure(f,"sync-copy-parent",rc); rc=close(f->parent); if(rc) error=failure(f,"close-copy-parent",rc); f->parent=-1; }
    if(f->root>=0) { rc=close(f->root); if(rc) error=failure(f,"close-directory",rc); f->root=-1; }
    if(f->base>=0) { rc=close(f->base); if(rc) error=failure(f,"close-base",rc); f->base=-1; }
    if(f->data>=0) { rc=close(f->data); if(rc) error=failure(f,"close-data",rc); f->data=-1; }
    if(error) fprintf(f->log,"stage=copy-cleanup-warning directory=%s temp=%s; inspect owned entries\n",f->directory_path,f->temporary);
    return error;
}
int psmtp_copy_file_discard(psmtp_copy_file *f){
    int rc,e,root;psmtp_native_stat st;const char *leaf;
    if(!f || !f->owned_directory)return 0;

    leaf=strrchr(f->directory_path,'/');
    if(!leaf || strncmp(leaf+1,"psmtp-",6) || strncmp(f->directory_path,PSMTP_COPY_DIRECTORY "/",strlen(PSMTP_COPY_DIRECTORY)+1))return MTP_VERIFY;
    e=chain(f);if(e)return e;
    if(f->owned || f->published){const char *path=f->published?f->path:f->temporary;
        e=identity(f,path,f->file_dev,f->file_ino,0);if(e)return e;
        if(unlink(path))return failure(f,"discard-payload",-1);
        f->owned=f->published=0;
    }
    e=chain(f);if(e)return e;
    if(rmdir(f->directory_path))return failure(f,"discard-directory",-1);f->owned_directory=0;
    root=open(PSMTP_COPY_DIRECTORY,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(root<0)return failure(f,"discard-root",root);
    e=status(f,root,NULL,&st);
    if(!e && ((uint64_t)st.st_dev!=f->root_dev || (uint64_t)st.st_ino!=f->root_ino))e=MTP_VERIFY;
    if(!e && fsync(root))e=failure(f,"discard-sync",-1);rc=close(root);if(rc && !e)e=failure(f,"discard-close",rc);
    if(f->log)fprintf(f->log,"stage=install-staging-discard status=%d payload-removed=1 directory-removed=1\n",e);
    return e;
}
void psmtp_copy_file_init(psmtp_copy_file *f,FILE *out,mtp_copy_sink *sink) {
    memset(f,0,sizeof(*f)); f->parent=f->file=f->directory=f->root=f->base=f->data=-1; f->log=out;
    sink->user=f; sink->begin=begin; sink->space=space; sink->write=put; sink->sync=sync_file;
    sink->rewind=rewind_file; sink->read=get; sink->publish=publish; sink->dispose=dispose; sink->prepare=NULL;
}
int psmtp_copy_file_init_at(psmtp_copy_file *f,FILE *out,mtp_copy_sink *sink,const char *parent) {
    size_t n=strlen(parent),base=strlen(PSMTP_COPY_DIRECTORY),i;
    psmtp_copy_file_init(f,out,sink);
    if(n>=sizeof(f->parent_path) || n<=base || strncmp(parent,PSMTP_COPY_DIRECTORY,base) || parent[base]!='/') return MTP_STATE;
    for(i=base+1;i<n;++i) if(!((parent[i]>='a' && parent[i]<='z') || (parent[i]>='0' && parent[i]<='9') || parent[i]=='-' || parent[i]=='/' || parent[i]=='_')) return MTP_STATE;
    if(parent[n-1]=='/' || strstr(parent,"//")) return MTP_STATE;
    memcpy(f->parent_path,parent,n+1); return 0;
}
