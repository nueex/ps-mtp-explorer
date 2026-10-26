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
#include "file_store.h"
#include "psmtp/copy_paths.h"
#include "psmtp/ui_format.h"
#include "stat_abi.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#ifdef PSMTP_TEST_FILE_SYSTEM
#include "ps4_copy_test_api.h"
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#endif
#include "dirent_check.h"
typedef char store_dirent_layout[(offsetof(struct dirent,d_name)==8)?1:-1];
#define RECEIPT ".psmtp-meta"
#define COMPLETE ".psmtp-complete"
typedef struct { char magic[8]; uint32_t version,kind; uint64_t bytes; uint8_t digest[32]; char data_name[16],name[MTP_STRING_BYTES]; } store_receipt;
static int store_join(char *out,const char *parent,const char *name) {
    size_t a=strlen(parent),b=strlen(name);
    if(!b || strchr(name,'/') || strchr(name,'\\') || !strcmp(name,".") || !strcmp(name,"..") || a+b+1>=PSMTP_LOCAL_PATH) return MTP_LIMIT;
    memcpy(out,parent,a);if(a==1 && parent[0]=='/') --a;out[a]='/';memcpy(out+a+1,name,b+1);return 0;
}
static int store_stat(int fd,psmtp_native_stat *out) {
    union { psmtp_native_stat st; uint8_t padding[512]; } data;
    memset(&data,0,sizeof(data)); if(fstat(fd,(void *)&data.st)) return MTP_FILESYSTEM;
    *out=data.st; return 0;
}
static int store_directory(const char *path) {
    char walk[PSMTP_LOCAL_PATH]; size_t n=strlen(path),i,base=strlen(PSMTP_COPY_DIRECTORY); int fd=-1;
    if(n>=sizeof(walk) || strncmp(path,PSMTP_COPY_DIRECTORY,base) || (path[base] && path[base]!='/')) { errno=EINVAL; return -1; }
    memcpy(walk,path,n+1);
    for(i=1;i<=n;++i) if(walk[i]=='/' || !walk[i]) {
        char save=walk[i]; walk[i]=0;
        if((i>1 && walk[i-1]=='/') || (i>1 && walk[i-1]=='.')) { errno=EINVAL; return -1; }
        fd=open(walk,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        walk[i]=save; if(fd<0) return -1;
        if(i<n) { if(close(fd)) return -1; fd=-1; }
    }
    return fd;
}

static int browse_directory(const char *path,const char *root) {
    char walk[PSMTP_LOCAL_PATH];size_t n=strlen(path),base=strlen(root),i,start=1;int fd;
    if(!base || root[0]!='/' || path[0]!='/' || n>=sizeof(walk) || n<base ||
       (base>1 && (strncmp(path,root,base) || (path[base] && path[base]!='/')))) {errno=EINVAL;return -1;}
    for(i=1;i<=n;++i) if(path[i]=='/' || !path[i]) {
        size_t length=i-start;
        if((!length && n>1) || (length==1 && path[start]=='.') ||
           (length==2 && path[start]=='.' && path[start+1]=='.') || memchr(path+start,'\\',length)) {errno=EINVAL;return -1;}
        start=i+1;
    }
    strcpy(walk,path);
    if(base==9 && !strncmp(root,"/mnt/usb",8) && root[8]>='0' && root[8]<='7') {
        char end=walk[base];walk[base]=0;fd=open(walk,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);walk[base]=end;
        if(fd<0 || n==base) return fd;
        if(close(fd)) return -1;
        for(i=base+1;i<=n;++i) if(path[i]=='/' || !path[i]) {
            end=walk[i];walk[i]=0;fd=open(walk,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);walk[i]=end;
            if(fd<0) return -1;
            if(i<n && close(fd)) return -1;
        }
        return fd;
    }
    if(n==1) return open("/",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    for(i=1;i<=n;++i) if(path[i]=='/' || !path[i]) {
        char end=walk[i];walk[i]=0;fd=open(walk,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);walk[i]=end;
        if(fd<0) return -1;
        if(i<n && close(fd)) return -1;
    }
    return fd;
}
unsigned psmtp_local_usb_mask(void) {return psmtp_local_usb_scan(NULL);}
int psmtp_local_prepare_copies(FILE *log) {
    const char *paths[]={PSMTP_LOG_DIRECTORY,PSMTP_COPY_DIRECTORY};size_t i;
    for(i=0;i<2;++i) {
        int fd,rc,error=0;psmtp_native_stat st;


        rc=mkdir(paths[i],0755);
        if(rc && errno!=EEXIST) error=errno;
        if(!error) {
            fd=psmtp_native_directory("/",paths[i]);
            if(fd<0) error=errno;
            else {
                if(store_stat(fd,&st)) error=errno;
                else if(!S_ISDIR(st.st_mode)) error=ENOTDIR;
                if(close(fd) && !error) error=errno;
            }
        }
        if(log) fprintf(log,"stage=storage-prepare directory=%s status=%d errno=%d existing-modes-changed=0\n",i?"copies":"logs",error?MTP_FILESYSTEM:0,error);
        if(error) {errno=error;return MTP_FILESYSTEM;}
    }
    return 0;
}
unsigned psmtp_local_usb_scan(FILE *log) {
    unsigned slot,mask=0;int fd;psmtp_native_stat st;char path[32];


    for(slot=0;slot<8;++slot) {
        snprintf(path,sizeof(path),"/mnt/usb%u",slot);
        fd=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        if(fd>=0) {
            int rc=store_stat(fd,&st),error=rc?errno:0;



            int placeholder=!rc && S_ISDIR(st.st_mode) && !(st.st_mode&0222) && !st.st_size && !st.st_blocks;
            if(!rc && S_ISDIR(st.st_mode) && !placeholder) mask|=1u<<slot;
            if(close(fd)) {error=errno;mask&=~(1u<<slot);}
            if(log) fprintf(log,"stage=usb-mount slot=%u accessible=%d placeholder=%d errno=%d dev=%" PRIu64 " mode=%04x method=direct-directory\n",slot,!!(mask&(1u<<slot)),placeholder,error,rc?0:(uint64_t)st.st_dev,rc?0:(unsigned)st.st_mode);
        } else if(log) fprintf(log,"stage=usb-mount slot=%u accessible=0 errno=%d\n",slot,errno);
    }
    return mask;
}
int psmtp_local_browse(psmtp_local_browser *b,const char *root,const char *path) {
    char saved[PSMTP_LOCAL_PATH];int fd;
    if(strlen(root)>=sizeof(saved)) return MTP_LIMIT;
    strcpy(saved,root);
    if(strlen(path)>=sizeof(b->path)) return MTP_LIMIT;
    {int (*progress)(void *,size_t,size_t)=b->scan_progress;void *user=b->scan_user;
    memset(b,0,sizeof(*b));b->scan_progress=progress;b->scan_user=user;}strcpy(b->root,saved);strcpy(b->path,path);
    fd=browse_directory(saved,saved);if(fd<0) {b->error=errno;b->error_stage="root-open";return MTP_FILESYSTEM;}if(close(fd)) {b->error=errno;b->error_stage="root-close";return MTP_FILESYSTEM;}
    return psmtp_local_load(b,path);
}
static int store_write_all(int fd,const void *buffer,size_t n) {
    const uint8_t *p=buffer;
    while(n) { ssize_t k=write(fd,p,n); if(k<0 && errno==EINTR) continue; if(k<=0 || (size_t)k>n) return MTP_FILESYSTEM; p+=k;n-=(size_t)k; }
    return 0;
}
static int store_read_all(int fd,void *buffer,size_t n) {
    uint8_t *p=buffer;
    while(n) { ssize_t k=read(fd,p,n); if(k<0 && errno==EINTR) continue; if(k<=0 || (size_t)k>n) return MTP_FILESYSTEM; p+=k;n-=(size_t)k; }
    return 0;
}
int psmtp_store_receipt(const char *directory,const char *name,uint64_t bytes,int folder,const uint8_t *digest,const char *data_name,FILE *log) {
    store_receipt receipt;char path[PSMTP_LOCAL_PATH];int dir,fd,rc=0;psmtp_native_stat st;
    if(!folder && (!digest || !data_name || strlen(data_name)>=sizeof(receipt.data_name))) return MTP_STATE;
    dir=store_directory(directory);if(dir<0) return MTP_FILESYSTEM;
    memset(&receipt,0,sizeof(receipt));memcpy(receipt.magic,"PSMTPM1",8);receipt.version=1;receipt.kind=folder?2:1;receipt.bytes=bytes;
    if(digest) memcpy(receipt.digest,digest,32);
    if(!folder) {
        int data_fd;
        if(store_join(path,directory,data_name)) {close(dir);return MTP_STATE;}
        data_fd=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
        if(data_fd<0) {close(dir);return MTP_FILESYSTEM;}
        rc=store_stat(data_fd,&st);
        if(!rc && (!S_ISREG(st.st_mode) || st.st_size<0 || (uint64_t)st.st_size!=bytes)) rc=MTP_VERIFY;
        if(close(data_fd)) rc=MTP_FILESYSTEM;
        if(rc) {close(dir);return rc;}
        strcpy(receipt.data_name,data_name);
    }
    if(strlen(name)>=sizeof(receipt.name)) { close(dir);return MTP_LIMIT; }
    strcpy(receipt.name,name);
    rc=store_join(path,directory,RECEIPT);if(rc) {close(dir);return rc;}
    fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(fd<0) {close(dir);return MTP_FILESYSTEM;}
    rc=store_stat(fd,&st);if(!rc && !S_ISREG(st.st_mode)) rc=MTP_FILESYSTEM;
    if(!rc) rc=store_write_all(fd,&receipt,sizeof(receipt));
    if(!rc && fsync(fd)) rc=MTP_FILESYSTEM;
    if(close(fd)) rc=MTP_FILESYSTEM;
    if(!rc && fsync(dir)) rc=MTP_FILESYSTEM;
    if(close(dir)) rc=MTP_FILESYSTEM;
    if(log) fprintf(log,"stage=copy-receipt status=%d kind=%s names-in-log=0\n",rc,folder?"folder":"file");
    return rc;
}
static int store_load_receipt(const char *directory,store_receipt *r) {
    char path[PSMTP_LOCAL_PATH];int fd,rc;psmtp_native_stat st;
    if(store_join(path,directory,RECEIPT)) return MTP_LIMIT;
    fd=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);if(fd<0) return MTP_FILESYSTEM;
    rc=store_stat(fd,&st);
    if(!rc && (!S_ISREG(st.st_mode) || st.st_size!=(int64_t)sizeof(*r))) rc=MTP_VERIFY;
    if(!rc) rc=store_read_all(fd,r,sizeof(*r));
    if(close(fd)) rc=MTP_FILESYSTEM;
    if(!rc && (memcmp(r->magic,"PSMTPM1",8) || r->version!=1 || (r->kind!=1 && r->kind!=2) || !memchr(r->name,0,sizeof(r->name)) || !memchr(r->data_name,0,sizeof(r->data_name)))) rc=MTP_VERIFY;
    return rc;
}
int psmtp_store_rename_label(const char *directory,const char *name) {
    store_receipt receipt;char path[256];int fd,rc;psmtp_native_stat st;size_t base=strlen(PSMTP_COPY_DIRECTORY);
    if(strncmp(directory,PSMTP_COPY_DIRECTORY,base) || directory[base]!='/' || store_load_receipt(directory,&receipt) || receipt.kind!=2)return 0;
    if(strlen(name)>=sizeof(receipt.name))return MTP_LIMIT;
    rc=store_join(path,directory,RECEIPT);if(rc)return rc;
    fd=open(path,O_RDWR|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return MTP_FILESYSTEM;
    rc=store_stat(fd,&st);if(!rc && (!S_ISREG(st.st_mode) || st.st_size!=(int64_t)sizeof(receipt)))rc=MTP_VERIFY;
    if(!rc){snprintf(receipt.name,sizeof(receipt.name),"%s",name);rc=store_write_all(fd,&receipt,sizeof(receipt));}
    if(!rc && fsync(fd))rc=MTP_FILESYSTEM;if(close(fd))rc=MTP_FILESYSTEM;return rc;
}
static int store_complete(const char *directory) {
    char path[PSMTP_LOCAL_PATH];int fd,ok;psmtp_native_stat st;
    if(store_join(path,directory,COMPLETE)) return 0;
    fd=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);if(fd<0) return 0;
    ok=!store_stat(fd,&st) && S_ISREG(st.st_mode) && st.st_size==0; if(close(fd)) ok=0;return ok;
}
static int store_sort_mode;
static int store_entry_compare(const void *a,const void *b) {
    const psmtp_local_entry *x=a,*y=b;
    return psmtp_ui_compare(store_sort_mode,x->name,x->folder,x->bytes,x->modified,y->name,y->folder,y->bytes,y->modified);
}
void psmtp_local_sort(psmtp_local_browser *b,int mode) {
    char selected[PSMTP_LOCAL_PATH]="";size_t i;if(mode<0 || mode>=PSMTP_SORT_COUNT)return;
    if(b->selected<b->count)strcpy(selected,b->entries[b->selected].path);
    b->sort_mode=store_sort_mode=mode;qsort(b->entries,b->count,sizeof(b->entries[0]),store_entry_compare);
    for(i=0;i<b->count;++i)if(!strcmp(selected,b->entries[i].path)){b->selected=i;break;}
}
int psmtp_local_load(psmtp_local_browser *b,const char *path) {
    DIR *d;struct dirent *entry;int pin;size_t selected=b->selected;char safe[PSMTP_LOCAL_PATH],root[PSMTP_LOCAL_PATH];int staging,cancelled=0;size_t visited=0;
    int (*progress)(void *,size_t,size_t)=b->scan_progress;void *user=b->scan_user;int sort_mode=b->sort_mode;
    strcpy(root,b->root[0]?b->root:PSMTP_COPY_DIRECTORY);
    if(strlen(path)>=sizeof(safe)) return MTP_LIMIT;
    strcpy(safe,path);memset(b,0,sizeof(*b));strcpy(b->path,safe);strcpy(b->root,root);b->scan_progress=progress;b->scan_user=user;b->sort_mode=sort_mode;
    staging=!strncmp(safe,PSMTP_COPY_DIRECTORY,strlen(PSMTP_COPY_DIRECTORY)) && (!safe[strlen(PSMTP_COPY_DIRECTORY)] || safe[strlen(PSMTP_COPY_DIRECTORY)]=='/');
    pin=browse_directory(safe,root);
    if(pin<0) {b->error=errno;b->error_stage="path-open";return MTP_FILESYSTEM;}
    d=opendir(safe);if(!d) {b->error=errno;b->error_stage="directory-open";close(pin);return MTP_FILESYSTEM;}
    for(;;) {
        char child[PSMTP_LOCAL_PATH];psmtp_native_stat st;int fd;psmtp_local_entry *item;store_receipt r;
        if(progress && progress(user,visited,0)) {b->error=ECANCELED;b->error_stage="cancelled";cancelled=1;break;}
        ++visited;errno=0;entry=readdir(d);if(!entry) {if(errno) {b->error=errno;b->error_stage="directory-read";}break;}
        if(!psmtp_dirent_valid(entry)) {b->error=EIO;b->error_stage="directory-record";break;}
        if(!strcmp(entry->d_name,".") || !strcmp(entry->d_name,"..")) continue;
        if(staging && (!strcmp(entry->d_name,RECEIPT) || !strcmp(entry->d_name,COMPLETE) || !strcmp(entry->d_name,"transfer.part") || !strcmp(entry->d_name,"reserve.part"))) continue;
        if(entry->d_type && entry->d_type!=4 && entry->d_type!=8) {++b->skipped;continue;}
        if(store_join(child,safe,entry->d_name)) {++b->skipped;continue;}
        fd=open(child,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);if(fd<0) {++b->skipped;continue;}
        if(store_stat(fd,&st)) {close(fd);++b->skipped;continue;} if(close(fd)) {b->error=errno;b->error_stage="entry-close";break;}
        if(!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode)) {++b->skipped;continue;}
        if(b->count==PSMTP_LOCAL_ITEMS) {b->truncated=1;break;}
        item=&b->entries[b->count];strcpy(item->path,child);strcpy(item->name,entry->d_name);item->folder=S_ISDIR(st.st_mode);item->bytes=st.st_size<0?0:(uint64_t)st.st_size;
        if(st.modify_time.seconds>0) {time_t changed=(time_t)st.modify_time.seconds;struct tm *date=localtime(&changed);if(date) strftime(item->modified,sizeof(item->modified),"%Y%m%dT%H%M%S",date);}
        if(staging && item->folder && !store_load_receipt(child,&r)) {
            strcpy(item->name,r.name);item->bytes=r.bytes;
            if(r.kind==1) {
                char file[PSMTP_LOCAL_PATH];int found=0;
                if(!store_join(file,child,r.data_name)) {
                    fd=open(file,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
                    if(fd>=0) {
                    if(!store_stat(fd,&st) && S_ISREG(st.st_mode) && st.st_size>=0 && (uint64_t)st.st_size==r.bytes) {found=1;strcpy(item->path,file);}
                    if(close(fd)) found=0;
                    }
                }
                if(found) {item->folder=0;item->verified=1;} else item->incomplete=1;
            } else item->incomplete=!store_complete(child);
        }
        ++b->count;
    }
    if(closedir(d) && !b->error) {b->error=errno;b->error_stage="directory-close";}
    if(close(pin) && !b->error) {b->error=errno;b->error_stage="path-close";}
    store_sort_mode=b->sort_mode;qsort(b->entries,b->count,sizeof(b->entries[0]),store_entry_compare);
    b->selected=b->count?(selected<b->count?selected:b->count-1):0;
    return cancelled?MTP_CANCELLED:b->error?MTP_FILESYSTEM:0;
}
int psmtp_local_enter(psmtp_local_browser *b) {
    char path[PSMTP_LOCAL_PATH];
    if(b->selected>=b->count || !b->entries[b->selected].folder) return MTP_STATE;
    strcpy(path,b->entries[b->selected].path);b->selected=0;return psmtp_local_load(b,path);
}
int psmtp_local_back(psmtp_local_browser *b) {
    char parent[PSMTP_LOCAL_PATH],child[PSMTP_LOCAL_PATH],*slash;size_t i;int rc;size_t base=strlen(b->root[0]?b->root:PSMTP_COPY_DIRECTORY);
    if(strlen(b->path)<=base) return MTP_STATE;
    strcpy(child,b->path);strcpy(parent,b->path);slash=strrchr(parent,'/');if(!slash) return MTP_STATE;
    if(slash==parent && base==1) slash[1]=0;
    else {if((size_t)(slash-parent)<base) return MTP_STATE;*slash=0;}
    b->selected=0;rc=psmtp_local_load(b,parent);
    if(!rc) for(i=0;i<b->count;++i) if(!strcmp(b->entries[i].path,child)) {b->selected=i;break;}
    return rc;
}
static int store_pin_node(psmtp_folder_node *node) {
    int fd=store_directory(node->path);psmtp_native_stat st;
    if(fd<0) return -1;
    if(store_stat(fd,&st) || !S_ISDIR(st.st_mode) || (uint64_t)st.st_dev!=node->dev || (uint64_t)st.st_ino!=node->ino) {close(fd);errno=EIO;return -1;}
    return fd;
}
int psmtp_folder_begin(psmtp_folder_store *store,const mtp_copy_plan *plan,FILE *log) {
    size_t i;unsigned attempt;char name[96];int fd,rc;uint64_t stamp=(uint64_t)time(NULL);psmtp_native_stat st;
    memset(store,0,sizeof(*store));store->log=log;
    if(!plan->count || plan->count>MTP_PLAN_ITEMS || !plan->items[0].folder) return MTP_STATE;
    store->count=plan->count;

    fd=open(PSMTP_LOG_DIRECTORY,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(fd<0) return MTP_FILESYSTEM;
    if(close(fd)) return MTP_FILESYSTEM;
    rc=mkdir(PSMTP_COPY_DIRECTORY,0755);if(rc && errno!=EEXIST) return MTP_FILESYSTEM;
    fd=store_directory(PSMTP_COPY_DIRECTORY);if(fd<0) return MTP_FILESYSTEM;if(close(fd)) return MTP_FILESYSTEM;
    for(i=0;i<plan->count;++i) if(plan->items[i].folder) {
        psmtp_folder_node *node=&store->nodes[i];const char *parent;
        if(!i) {
            for(attempt=0;attempt<128;++attempt) {
                snprintf(name,sizeof(name),"psmtp-folder-%" PRIu64 "-%08" PRIx32 "-%u",stamp,plan->items[0].source.handle,attempt);
                rc=store_join(node->path,PSMTP_COPY_DIRECTORY,name);if(rc) return rc;
                if(!mkdir(node->path,0700)) break;
                if(errno!=EEXIST) return MTP_FILESYSTEM;
            }
            if(attempt==128) return MTP_LIMIT;
        } else {
            if(plan->items[i].parent>=i || !store->nodes[plan->items[i].parent].created) return MTP_STATE;
            parent=store->nodes[plan->items[i].parent].path;
            fd=store_pin_node(&store->nodes[plan->items[i].parent]);if(fd<0) return MTP_FILESYSTEM;if(close(fd)) return MTP_FILESYSTEM;
            snprintf(name,sizeof(name),"dir-%08" PRIx32,plan->items[i].source.handle);
            rc=store_join(node->path,parent,name);if(rc) return rc;
            if(mkdir(node->path,0700)) return MTP_FILESYSTEM;
        }
        fd=store_directory(node->path);if(fd<0) return MTP_FILESYSTEM;
        rc=store_stat(fd,&st);if(close(fd)) rc=MTP_FILESYSTEM;if(rc) return rc;
        node->dev=st.st_dev;node->ino=st.st_ino;node->created=1;
        rc=psmtp_store_receipt(node->path,plan->items[i].source.info.name,i?0:plan->bytes,1,NULL,NULL,log);if(rc) return rc;
    }
    fprintf(log,"stage=folder-destination path=%s entries=%zu\n",store->nodes[0].path,plan->count);return 0;
}
const char *psmtp_folder_parent(const psmtp_folder_store *store,const mtp_copy_plan *plan,size_t index) {
    size_t parent;
    if(index>=plan->count) return NULL;
    parent=plan->items[index].parent;
    return parent<store->count && store->nodes[parent].created?store->nodes[parent].path:NULL;
}
int psmtp_folder_finish(psmtp_folder_store *store,int complete) {
    size_t i;int error=0;
    for(i=store->count;i>0;--i) if(store->nodes[i-1].created) {
        psmtp_folder_node *node=&store->nodes[i-1];int fd=store_pin_node(node),rc;psmtp_native_stat st;
        if(fd<0) {error=MTP_FILESYSTEM;continue;}
        if(fchmod(fd,0755) || store_stat(fd,&st) || (st.st_mode&0777)!=0755) error=MTP_FILESYSTEM;
        if(fsync(fd)) error=MTP_FILESYSTEM;
        if(complete && !error) {
            char path[PSMTP_LOCAL_PATH];int mark;
            rc=store_join(path,node->path,COMPLETE);if(rc) {close(fd);error=rc;continue;}
            mark=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
            if(mark<0) error=MTP_FILESYSTEM;
            else {if(fsync(mark)) error=MTP_FILESYSTEM;if(close(mark)) error=MTP_FILESYSTEM;}
            if(fsync(fd)) error=MTP_FILESYSTEM;
        }
        if(close(fd)) error=MTP_FILESYSTEM;
    }
    fprintf(store->log,"stage=folder-finish complete=%d status=%d names-in-log=0\n",complete && !error,error);
    return error;
}

int psmtp_native_directory(const char *root,const char *path) {return browse_directory(path,root);}
int psmtp_native_status(int fd,psmtp_native_stat *st) {return store_stat(fd,st);}




int psmtp_store_remove_file_wrapper(const char *file,FILE *log) {
    char parent[256],meta[256],*slash;store_receipt receipt;psmtp_native_stat parent_st,meta_st,check;DIR *d;struct dirent *e;int dir=-1,fd=-1,rc=0,only_receipt=1;
    size_t base=strlen(PSMTP_COPY_DIRECTORY);
    if(strncmp(file,PSMTP_COPY_DIRECTORY,base) || file[base]!='/' || strlen(file)>=sizeof(parent)) return 0;
    strcpy(parent,file);slash=strrchr(parent,'/');if(!slash || slash==parent) return 0;*slash=0;
    if(strncmp(strrchr(parent,'/')+1,"psmtp-",6) || store_load_receipt(parent,&receipt) || receipt.kind!=1 || strcmp(receipt.data_name,strrchr(file,'/')+1)) return 0;
    dir=store_directory(parent);if(dir<0) return MTP_FILESYSTEM;
    if(store_stat(dir,&parent_st) || store_join(meta,parent,RECEIPT)) {close(dir);return MTP_FILESYSTEM;}
    fd=open(file,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);if(fd>=0) {close(fd);close(dir);return 0;}if(errno!=ENOENT) {close(dir);return MTP_FILESYSTEM;}
    fd=open(meta,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0) {close(dir);return MTP_FILESYSTEM;}
    rc=store_stat(fd,&meta_st);if(close(fd)) rc=MTP_FILESYSTEM;if(rc) {close(dir);return rc;}
    d=opendir(parent);if(!d) {close(dir);return MTP_FILESYSTEM;}
    for(;;) {errno=0;e=readdir(d);if(!e) {if(errno) rc=MTP_FILESYSTEM;break;}
        if(!psmtp_dirent_valid(e)) {rc=MTP_PROTOCOL;break;}
        if(strcmp(e->d_name,".") && strcmp(e->d_name,"..") && strcmp(e->d_name,RECEIPT)) {only_receipt=0;break;}}
    if(closedir(d)) rc=MTP_FILESYSTEM;
    if(!rc && only_receipt) {
        fd=store_directory(parent);if(fd<0) rc=MTP_FILESYSTEM;
        else {if(store_stat(fd,&check) || check.st_dev!=parent_st.st_dev || check.st_ino!=parent_st.st_ino) rc=MTP_VERIFY;if(close(fd)) rc=MTP_FILESYSTEM;}
        if(!rc) {fd=open(meta,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0) rc=MTP_FILESYSTEM;else {if(store_stat(fd,&check) || check.st_dev!=meta_st.st_dev || check.st_ino!=meta_st.st_ino) rc=MTP_VERIFY;if(close(fd)) rc=MTP_FILESYSTEM;}}
        if(!rc && unlink(meta)) rc=MTP_FILESYSTEM;
        if(!rc && fsync(dir)) rc=MTP_FILESYSTEM;
    }
    if(close(dir)) rc=MTP_FILESYSTEM;
    if(!rc && only_receipt && rmdir(parent)) rc=MTP_FILESYSTEM;
    if(log) fprintf(log,"stage=legacy-file-wrapper-cleanup status=%d removed=%d errno=%d names-in-log=0\n",rc,!rc && only_receipt,errno);
    return rc;
}
