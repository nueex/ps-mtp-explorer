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
#include "file_ops.h"
#include "storage_space.h"
#include "psmtp/sha256.h"
#include "psmtp/ui_format.h"
#include "psmtp/name.h"
#include "psmtp/navigator.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#ifdef PSMTP_TEST_FILE_SYSTEM
#include "ps4_copy_test_api.h"
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#endif
#include "dirent_check.h"
static int ops_error(psmtp_op_plan *p,const char *stage,int rc) {
    int error=p->error_stage?p->error_number:rc==MTP_FILESYSTEM?errno:0;
    if(p->log) fprintf(p->log,"stage=operation-%s status=%d errno=%d failed-at=%s copied=%zu removed=%zu names-in-log=0\n",stage,rc,error,p->error_stage?p->error_stage:"none",p->copied,p->removed);
    errno=error;
    return rc;
}
static int ops_failure(psmtp_op_plan *p,const char *stage,int rc) {
    int error=rc==MTP_FILESYSTEM?errno:0;
    if(!p->error_stage){p->error_stage=stage;p->error_number=error;}
    if(p->log)fprintf(p->log,"stage=operation-fs status=%d failed-at=%s errno=%d names-in-log=0\n",rc,stage,error);
    errno=error;return rc;
}
static int ops_name(const char *name) {
    size_t i,n=strlen(name);if(!n || n>255 || !strcmp(name,".") || !strcmp(name,"..")) return 0;
    for(i=0;i<n;++i) if((unsigned char)name[i]<32 || name[i]=='/' || name[i]=='\\') return 0;
    return 1;
}
static int ops_join(char *out,const char *parent,const char *name) {
    size_t a=strlen(parent),b=strlen(name);if(!ops_name(name)) return MTP_STATE;
    if(a==1 && parent[0]=='/') a=0;
    if(a+b+1>=256) return MTP_LIMIT;
    memcpy(out,parent,a);out[a]='/';memcpy(out+a+1,name,b+1);return 0;
}
static int ops_below(const char *path,const char *root) {
    size_t n=strlen(root);return !strncmp(path,root,n) && (!path[n] || path[n]=='/');
}
static int ops_write_area(const char *path,int removing) {
    if(ops_below(path,"/data")) {
        if(removing && (!strcmp(path,"/data") || !strcmp(path,"/data/psmtp"))) return 0;
        if(ops_below(path,"/data/psmtp") && !ops_below(path,"/data/psmtp/copies")) return 0;
        if(removing && !strcmp(path,"/data/psmtp/copies")) return 0;
        return 1;
    }
    if(strlen(path)>=9 && !strncmp(path,"/mnt/usb",8) && path[8]>='0' && path[8]<='7' && (!path[9] || path[9]=='/')) return !removing || path[9];
    return 0;
}
static int ops_same(const psmtp_native_stat *a,const psmtp_native_stat *b,int full) {
    if(a->st_dev!=b->st_dev || a->st_ino!=b->st_ino || (a->st_mode&S_IFMT)!=(b->st_mode&S_IFMT) || a->st_gen!=b->st_gen) return 0;
    return !full || (a->st_size==b->st_size && !memcmp(&a->modify_time,&b->modify_time,sizeof(a->modify_time)) && !memcmp(&a->change_time,&b->change_time,sizeof(a->change_time)));
}
static int ops_open_detail(const char *root,const char *path,int flags,const char **stage) {
    char parent[256],*slash;int fd;
    if(stage)*stage="path-validation";
    if(strlen(path)>=sizeof(parent)) {errno=ENAMETOOLONG;return -1;}
    strcpy(parent,path);slash=strrchr(parent,'/');if(!slash || !slash[1]) {errno=EINVAL;return -1;}
    if(slash==parent) slash[1]=0;else *slash=0;
    if(stage)*stage="parent-open";
    fd=psmtp_native_directory(root,parent);if(fd<0) return -1;
    if(stage)*stage="parent-close";
    if(close(fd)) return -1;
    if(stage)*stage=(flags&O_CREAT)?"destination-create":"leaf-open";
    return open(path,flags|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK,0600);
}
static int ops_open(const char *root,const char *path,int flags) {return ops_open_detail(root,path,flags,NULL);}
static int ops_native_check_detail(const char *root,const char *path,const psmtp_native_stat *expected,int full,const char **stage) {
    int fd;psmtp_native_stat st;int rc,error=0;if(stage)*stage="directory-open";
    fd=!strcmp(root,path)?psmtp_native_directory(root,path):ops_open_detail(root,path,O_RDONLY,stage);
    if(fd<0) return MTP_FILESYSTEM;
    if(stage)*stage="descriptor-stat";
    rc=psmtp_native_status(fd,&st);if(rc)error=errno;
    if(!rc && !ops_same(&st,expected,full)){rc=MTP_VERIFY;if(stage)*stage="identity-check";}
    if(close(fd) && !rc){rc=MTP_FILESYSTEM;error=errno;if(stage)*stage="descriptor-close";}
    if(rc==MTP_FILESYSTEM)errno=error;return rc;
}
static int ops_native_check(const char *root,const char *path,const psmtp_native_stat *expected,int full) {return ops_native_check_detail(root,path,expected,full,NULL);}
int psmtp_op_capture(psmtp_op_ref *out,const psmtp_location *location,const psmtp_local_entry *entry) {
    int fd,rc;memset(out,0,sizeof(*out));
    if(!entry || !ops_name(entry->name) || strlen(entry->path)>=sizeof(out->path)) return MTP_LIMIT;
    out->location=*location;strcpy(out->path,entry->path);strcpy(out->name,entry->name);out->folder=entry->folder;out->wrapped=entry->verified && !entry->folder;
    fd=ops_open(location->root,out->path,O_RDONLY);if(fd<0) return MTP_FILESYSTEM;
    rc=psmtp_native_status(fd,&out->native);if(close(fd)) rc=MTP_FILESYSTEM;
    if(!rc && (out->folder?!S_ISDIR(out->native.st_mode):!S_ISREG(out->native.st_mode))) rc=MTP_VERIFY;
    return rc;
}
static int ops_cancel(mtp_copy_progress progress,void *user,int phase,uint64_t done,uint64_t total) {return progress && progress(user,(enum mtp_copy_phase)phase,done,total)?MTP_CANCELLED:0;}
static int ops_add_native(psmtp_op_plan *p,const char *path,const char *name,size_t parent,size_t depth,mtp_copy_progress progress,void *user) {
    size_t at=p->count;psmtp_op_node *e;int fd,rc;DIR *dir;struct dirent *child;
    rc=ops_cancel(progress,user,PSMTP_OP_SCAN,p->count,0);if(rc) return rc;
    if(depth>MTP_PLAN_DEPTH || at==MTP_PLAN_ITEMS || !ops_name(name)) return MTP_LIMIT;
    e=&p->nodes[p->count++];strcpy(e->path,path);strcpy(e->name,name);e->parent=parent;
    fd=ops_open(p->source.location.root,path,O_RDONLY);if(fd<0) return MTP_FILESYSTEM;
    rc=psmtp_native_status(fd,&e->native);if(close(fd)) rc=MTP_FILESYSTEM;if(rc) return rc;
    if(S_ISREG(e->native.st_mode)) {
        if(e->native.st_size<0 || (uint64_t)e->native.st_size>UINT64_MAX/8 || UINT64_MAX-p->bytes<(uint64_t)e->native.st_size) return MTP_LIMIT;
        e->phone.size=(uint64_t)e->native.st_size;p->bytes+=e->phone.size;++p->files;return 0;
    }
    if(!S_ISDIR(e->native.st_mode)) return MTP_UNSUPPORTED;
    e->folder=1;++p->folders;dir=opendir(path);if(!dir) return MTP_FILESYSTEM;
    for(;;) {
        char next[256];errno=0;child=readdir(dir);if(!child) {rc=errno?MTP_FILESYSTEM:0;break;}
        if(!psmtp_dirent_valid(child)) {rc=MTP_PROTOCOL;break;}
        if(!strcmp(child->d_name,".") || !strcmp(child->d_name,"..")) continue;
        if(child->d_type && child->d_type!=4 && child->d_type!=8) {rc=MTP_UNSUPPORTED;break;}
        rc=ops_join(next,path,child->d_name);if(rc) break;
        rc=ops_add_native(p,next,child->d_name,at,depth+1,progress,user);if(rc) break;
    }
    if(closedir(dir)) rc=MTP_FILESYSTEM;return rc;
}
static int ops_info_same(const mtp_object_info *a,const mtp_object_info *b,int full) {
    return a->storage==b->storage && a->parent==b->parent && a->format==b->format && a->protection==b->protection && !strcmp(a->name,b->name) && (!full || (a->size32==b->size32 && !strcmp(a->modified,b->modified)));
}
static int ops_phone_check(mtp_client *client,const psmtp_op_node *e,int full) {
    mtp_object_info info;uint64_t bytes;int rc=mtp_get_object_info(client,e->phone.handle,&info);
    if(!rc && !ops_info_same(&e->phone.info,&info,full)) rc=MTP_VERIFY;
    if(!rc && full && !e->folder && e->phone.size_property) {rc=mtp_get_object_size(client,e->phone.handle,&bytes);if(!rc && bytes!=e->phone.size) rc=MTP_VERIFY;}
    return rc;
}
int psmtp_op_rename(const psmtp_op_ref *source,mtp_client *client,const char *name,char result[256],FILE *log) {
    int rc=0;result[0]=0;if(!psmtp_name_valid(name))return MTP_STATE;
    if(!strcmp(source->name,name)){snprintf(result,256,"%s",source->path);return 0;}
    if(source->location.kind==PSMTP_LOCATION_PHONE){
        mtp_device_info *device=malloc(sizeof(*device));mtp_object_info info;uint32_t *ids=malloc(MTP_MAX_ITEMS*sizeof(*ids));size_t count=0,i;
        if(!client || !device || !ids){free(device);free(ids);return MTP_MEMORY;}
        rc=mtp_get_device_info(client,device);
        if(!rc && (!source->identity_valid || !device->identity_valid || memcmp(source->phone_identity,device->identity,32)))rc=MTP_VERIFY;
        if(!rc && (!mtp_supports(device,0x9802) || !mtp_supports(device,0x9804)))rc=MTP_UNSUPPORTED;
        if(!rc)rc=mtp_get_object_info(client,source->handle,&info);
        if(!rc && (!ops_info_same(&source->info,&info,1) || info.protection))rc=MTP_VERIFY;
        if(!rc)rc=mtp_get_handles(client,info.storage,info.parent?info.parent:MTP_ROOT,ids,MTP_MAX_ITEMS,&count);
        for(i=0;!rc && i<count;++i)if(ids[i]!=source->handle){mtp_object_info peer;rc=mtp_get_object_info(client,ids[i],&peer);if(!rc && !psmtp_ui_name_compare(peer.name,name))rc=MTP_STATE;}
        if(!rc)rc=mtp_rename_object(client,source->handle,source->info.format,name);
        if(!rc){rc=mtp_get_object_info(client,source->handle,&info);if(!rc && (strcmp(info.name,name) || info.storage!=source->info.storage || info.parent!=source->info.parent || info.format!=source->info.format || info.size32!=source->info.size32))rc=MTP_VERIFY;}
        free(ids);free(device);
    }else{
        char parent[256],target[256],*slash;int pin=-1,fd=-1,reserved=0,have_destination=0;psmtp_native_stat destination;
        if(!ops_write_area(source->path,1))return MTP_UNSUPPORTED;
        strcpy(parent,source->path);slash=strrchr(parent,'/');if(!slash || slash==parent)return MTP_STATE;*slash=0;
        if(source->wrapped && source->directory[0])snprintf(parent,sizeof(parent),"%s",source->directory);
        rc=ops_join(target,parent,name);if(rc)return rc;
        pin=psmtp_native_directory(source->location.root,parent);if(pin<0)return MTP_FILESYSTEM;
        rc=ops_native_check(source->location.root,source->path,&source->native,!source->folder);


        if(!rc){if(source->folder){if(mkdir(target,0700))rc=MTP_FILESYSTEM;else {reserved=1;fd=ops_open(source->location.root,target,O_RDONLY|O_DIRECTORY);}}
            else {fd=ops_open(source->location.root,target,O_RDWR|O_CREAT|O_EXCL);if(fd>=0)reserved=1;else rc=MTP_FILESYSTEM;}}
        if(!rc && fd<0)rc=MTP_FILESYSTEM;
        if(!rc){rc=psmtp_native_status(fd,&destination);if(!rc)have_destination=1;}if(fd>=0 && close(fd))rc=MTP_FILESYSTEM;
        if(!rc)rc=ops_native_check(source->location.root,target,&destination,1);
        if(!rc)rc=ops_native_check(source->location.root,source->path,&source->native,!source->folder);
        if(!rc && rename(source->path,target))rc=MTP_FILESYSTEM;
        if(!rc){reserved=0;rc=ops_native_check(source->location.root,target,&source->native,0);snprintf(result,256,"%s",target);}
        if(reserved && have_destination && !ops_native_check(source->location.root,target,&destination,0)){if(source->folder)(void)rmdir(target);else (void)unlink(target);}
        if(fsync(pin) && !rc)rc=MTP_FILESYSTEM;if(close(pin) && !rc)rc=MTP_FILESYSTEM;
        if(!reserved && result[0] && source->folder){int label=psmtp_store_rename_label(result,name);if(!rc)rc=label;}
        if(!reserved && result[0] && source->wrapped)(void)psmtp_store_remove_file_wrapper(source->path,log);
    }
    if(log)fprintf(log,"stage=rename status=%d location=%d names-in-log=0\n",rc,source->location.kind);
    return rc;
}
typedef struct {mtp_copy_progress progress;void *user;} ops_scan_context;
static int ops_scan_phone(void *context,size_t files,size_t folders) {ops_scan_context *scan=context;return ops_cancel(scan->progress,scan->user,PSMTP_OP_SCAN,files+folders,0)!=0;}
int psmtp_op_plan_build(psmtp_op_plan *p,const psmtp_op_ref *source,mtp_client *client,mtp_copy_progress progress,void *user,FILE *log) {
    int rc;size_t i;ops_scan_context scan={progress,user};memset(p,0,sizeof(*p));p->source=*source;p->log=log;
    if(source->location.kind!=PSMTP_LOCATION_PHONE) {
        rc=ops_native_check(source->location.root,source->path,&source->native,!source->folder);
        if(!rc) rc=ops_add_native(p,source->path,source->name,SIZE_MAX,0,progress,user);
    } else {
        mtp_device_info *device=malloc(sizeof(*device));mtp_copy_plan *plan=NULL;
        if(!client || !device) {free(device);return MTP_STATE;}
        rc=mtp_get_device_info(client,device);
        if(!rc && (!source->identity_valid || !device->identity_valid || memcmp(source->phone_identity,device->identity,32))) rc=MTP_VERIFY;
        p->delete_supported=!rc && mtp_supports(device,0x100b);free(device);
        if(rc) return ops_error(p,"phone-identity",rc);
        if(source->folder) {
            plan=malloc(sizeof(*plan));if(!plan) return MTP_MEMORY;
            rc=mtp_copy_plan_build(plan,client,source->handle,&source->info,ops_scan_phone,&scan,log);
            if(!rc) {
                p->count=plan->count;p->files=plan->files;p->folders=plan->folders;p->bytes=plan->bytes;
                for(i=0;i<p->count;++i) {
                    psmtp_op_node *e=&p->nodes[i];e->phone=plan->items[i].source;e->phone.is_pkg=0;e->folder=plan->items[i].folder;e->parent=plan->items[i].parent;
                    if(!ops_name(e->phone.info.name)) {rc=MTP_LIMIT;break;}strcpy(e->name,e->phone.info.name);
                    rc=ops_cancel(progress,user,PSMTP_OP_SCAN,i,p->count);if(rc) break;
                }
            }
            free(plan);
        } else {
            p->count=p->files=1;p->nodes[0].parent=SIZE_MAX;
            rc=mtp_copy_probe(client,source->handle,&source->info,&p->nodes[0].phone,log);p->nodes[0].phone.is_pkg=0;
            if(!ops_name(source->name)) rc=MTP_LIMIT;
            if(!rc) {strcpy(p->nodes[0].name,source->name);p->bytes=p->nodes[0].phone.size;}
        }
    }
    return rc?ops_error(p,"preflight",rc):0;
}
int psmtp_op_target(psmtp_op_plan *p,const psmtp_location *location,const char *path) {
    size_t i;int fd,rc;if(!p->count || location->kind==PSMTP_LOCATION_PHONE || !ops_write_area(path,0)) return MTP_UNSUPPORTED;
    if(strlen(path)>=sizeof(p->destination)) return MTP_LIMIT;
    p->destination_location=*location;strcpy(p->destination,path);
    fd=psmtp_native_directory(location->root,path);if(fd<0) return MTP_FILESYSTEM;
    rc=psmtp_native_status(fd,&p->destination_stat);if(close(fd)) rc=MTP_FILESYSTEM;if(rc) return rc;
    for(i=0;i<p->count;++i) {
        psmtp_op_node *e=&p->nodes[i];const char *parent;
        if(i && e->parent>=i) return MTP_STATE;
        parent=i?p->nodes[e->parent].target:path;
        rc=ops_join(e->target,parent,e->name);if(rc) return rc;
    }
    if(p->source.location.kind!=PSMTP_LOCATION_PHONE && ops_below(p->nodes[0].target,p->source.path)) return MTP_STATE;
    return 0;
}
typedef struct {psmtp_op_plan *plan;psmtp_op_node *node;int fd,owned,published,destination_dir,parent_dir;uint64_t expected;} ops_file;
static int ops_destination_check(psmtp_op_plan *p) {const char *stage=NULL;int rc=ops_native_check_detail(p->destination_location.root,p->destination,&p->destination_stat,0,&stage);return rc?ops_failure(p,stage,rc):0;}
static int ops_owned_remove(const char *root,const char *path,const psmtp_native_stat *st) {int rc=ops_native_check(root,path,st,0);return rc?rc:unlink(path)?MTP_FILESYSTEM:0;}
static int ops_file_begin(void *user,const mtp_copy_source *source) {
    ops_file *f=user;const char *stage=NULL;int rc=ops_destination_check(f->plan);if(rc) return rc;
    f->destination_dir=psmtp_native_directory(f->plan->destination_location.root,f->plan->destination);
    if(f->destination_dir<0)return ops_failure(f->plan,"destination-pin",MTP_FILESYSTEM);
    f->parent_dir=f->destination_dir;
    if(f->node->parent!=SIZE_MAX) {psmtp_op_node *parent=&f->plan->nodes[f->node->parent];rc=ops_native_check_detail(f->plan->destination_location.root,parent->target,&parent->destination,0,&stage);if(rc) return ops_failure(f->plan,stage,rc);}
    if(f->node->parent!=SIZE_MAX){f->parent_dir=psmtp_native_directory(f->plan->destination_location.root,f->plan->nodes[f->node->parent].target);if(f->parent_dir<0)return ops_failure(f->plan,"parent-pin",MTP_FILESYSTEM);}
    f->expected=source->size;f->fd=ops_open_detail(f->plan->destination_location.root,f->node->target,O_RDWR|O_CREAT|O_EXCL,&stage);
    if(f->fd<0) return ops_failure(f->plan,stage,MTP_FILESYSTEM);f->owned=1;
    rc=psmtp_native_status(f->fd,&f->node->destination);return rc?ops_failure(f->plan,"destination-stat",rc):0;
}
static int ops_file_space(void *user,uint64_t *available) {
    ops_file *f=user;psmtp_native_stat st;int rc;*available=0;



    rc=psmtp_native_status(f->destination_dir,&st);if(rc)return rc;
    if(!ops_same(&st,&f->plan->destination_stat,0))return MTP_VERIFY;
    if(f->node->parent!=SIZE_MAX){rc=psmtp_native_status(f->parent_dir,&st);if(rc)return rc;if(!ops_same(&st,&f->plan->nodes[f->node->parent].destination,0))return MTP_VERIFY;}
    rc=psmtp_native_status(f->fd,&st);if(rc)return rc;
    if(!ops_same(&st,&f->node->destination,0) || st.st_size<0 || (uint64_t)st.st_size>f->expected)return MTP_VERIFY;
    return MTP_COPY_SPACE_STREAMING;
}
static int ops_file_write(void *user,const uint8_t *data,size_t n,size_t *actual) {ops_file *f=user;ssize_t k;do {k=write(f->fd,data,n);} while(k<0 && errno==EINTR);*actual=k<0?0:(size_t)k;return k<0?(errno==ENOSPC?MTP_NO_SPACE:MTP_FILESYSTEM):0;}
static int ops_file_sync(void *user) {return fsync(((ops_file *)user)->fd)?(errno==ENOSPC?MTP_NO_SPACE:MTP_FILESYSTEM):0;}
static int ops_file_rewind(void *user) {return lseek(((ops_file *)user)->fd,0,SEEK_SET)==0?0:MTP_FILESYSTEM;}
static int ops_file_read(void *user,uint8_t *data,size_t n,size_t *actual) {ops_file *f=user;ssize_t k;do {k=read(f->fd,data,n);} while(k<0 && errno==EINTR);*actual=k<0?0:(size_t)k;return k<0?MTP_FILESYSTEM:0;}
static int ops_sync_parent(const char *root,const char *path) {
    char parent[256],*slash;int fd,rc;strcpy(parent,path);slash=strrchr(parent,'/');if(!slash) return MTP_STATE;
    if(slash==parent) slash[1]=0;else *slash=0;fd=psmtp_native_directory(root,parent);if(fd<0) return MTP_FILESYSTEM;
    rc=fsync(fd)?MTP_FILESYSTEM:0;if(close(fd)) rc=MTP_FILESYSTEM;return rc;
}
static int ops_file_publish(void *user,int *published) {
    ops_file *f=user;psmtp_native_stat st;int rc=ops_destination_check(f->plan);if(rc) return rc;
    rc=ops_native_check(f->plan->destination_location.root,f->node->target,&f->node->destination,0);if(rc) return rc;
    if(f->plan->destination_location.kind!=PSMTP_LOCATION_USB && fchmod(f->fd,0644)) return MTP_FILESYSTEM;
    if(fsync(f->fd) || psmtp_native_status(f->fd,&st) || !ops_same(&st,&f->node->destination,0) || st.st_size<0 || (uint64_t)st.st_size!=f->expected) return MTP_VERIFY;
    f->node->destination=st;f->published=*published=1;return ops_sync_parent(f->plan->destination_location.root,f->node->target);
}
static int ops_file_dispose(void *user) {
    ops_file *f=user;int rc=0;if(f->fd>=0) {if(close(f->fd)) rc=MTP_FILESYSTEM;f->fd=-1;}
    if(f->owned && !f->published) {int e=ops_owned_remove(f->plan->destination_location.root,f->node->target,&f->node->destination);if(e) {rc=e;f->plan->cleanup_failed=1;}}
    if(f->parent_dir>=0 && f->parent_dir!=f->destination_dir && close(f->parent_dir))rc=MTP_FILESYSTEM;
    if(f->destination_dir>=0 && close(f->destination_dir))rc=MTP_FILESYSTEM;
    f->parent_dir=f->destination_dir=-1;
    return rc;
}
static int ops_hash_native(psmtp_op_plan *p,const char *root,const char *path,const psmtp_native_stat *expected,uint8_t digest[32],mtp_copy_progress progress,void *user,int phase) {
    int fd=ops_open(root,path,O_RDONLY),rc=0;uint8_t *data;uint64_t offset=0;psmtp_sha256 hash;psmtp_native_stat st;
    if(fd<0) return MTP_FILESYSTEM;
    data=malloc(MTP_COPY_CHUNK);if(!data) {close(fd);return MTP_MEMORY;}
    rc=psmtp_native_status(fd,&st);if(!rc && !ops_same(&st,expected,1)) rc=MTP_VERIFY;psmtp_sha256_init(&hash);
    while(!rc && offset<(uint64_t)expected->st_size) {
        size_t n=(size_t)((uint64_t)expected->st_size-offset>MTP_COPY_CHUNK?MTP_COPY_CHUNK:(uint64_t)expected->st_size-offset);ssize_t k;
        rc=ops_cancel(progress,user,phase,offset,(uint64_t)expected->st_size);if(rc) break;
        do {k=read(fd,data,n);} while(k<0 && errno==EINTR);if(k<=0 || (size_t)k>n) {rc=MTP_FILESYSTEM;break;}
        psmtp_sha256_update(&hash,data,(size_t)k);offset+=(uint64_t)k;
    }
    if(!rc) {ssize_t k=read(fd,data,1);if(k!=0) rc=MTP_VERIFY;}
    if(!rc && (psmtp_native_status(fd,&st) || !ops_same(&st,expected,1))) rc=MTP_VERIFY;
    if(!rc) psmtp_sha256_final(&hash,digest);free(data);if(close(fd)) rc=MTP_FILESYSTEM;
    if(rc) ops_error(p,"hash",rc);return rc;
}
static int ops_local_file(psmtp_op_plan *p,psmtp_op_node *e,mtp_copy_sink *sink,mtp_copy_progress progress,void *user,mtp_copy_result *result) {
    int fd=-1,rc,started=0;uint8_t *data=NULL,hash[32];uint64_t offset=0;psmtp_sha256 sha;mtp_copy_source source={0};
    memset(result,0,sizeof(*result));source.size=e->phone.size;
    rc=ops_native_check(p->source.location.root,e->path,&e->native,1);if(rc) return rc;
    data=malloc(MTP_COPY_CHUNK);if(!data) return MTP_MEMORY;
    started=1;rc=sink->begin(sink->user,&source);if(rc) goto done;
    fd=ops_open(p->source.location.root,e->path,O_RDONLY);if(fd<0) {rc=MTP_FILESYSTEM;goto done;}
    psmtp_sha256_init(&sha);
    while(offset<source.size) {
        size_t n=(size_t)(source.size-offset>MTP_COPY_CHUNK?MTP_COPY_CHUNK:source.size-offset),written=0;ssize_t k;
        rc=ops_cancel(progress,user,MTP_COPY_RECEIVE,offset,source.size);if(rc) goto done;
        do {k=read(fd,data,n);} while(k<0 && errno==EINTR);if(k<=0 || (size_t)k>n) {rc=MTP_FILESYSTEM;goto done;}
        psmtp_sha256_update(&sha,data,(size_t)k);
        while(written<(size_t)k) {size_t actual=0;rc=(sink->write)(sink->user,data+written,(size_t)k-written,&actual);if(rc || !actual || actual>(size_t)k-written) {if(!rc) rc=MTP_FILESYSTEM;goto done;}written+=actual;}
        offset+=(uint64_t)k;
    }
    rc=ops_cancel(progress,user,MTP_COPY_RECEIVE,source.size,source.size);if(rc)goto done;
    if(read(fd,data,1)!=0) {rc=MTP_VERIFY;goto done;}
    if(close(fd)) {fd=-1;rc=MTP_FILESYSTEM;goto done;}fd=-1;psmtp_sha256_final(&sha,result->digest);
    rc=sink->sync(sink->user);if(rc) goto done;
    {ops_file *f=sink->user;psmtp_native_stat st;rc=psmtp_native_status(f->fd,&st);if(!rc) rc=ops_hash_native(p,p->destination_location.root,e->target,&st,hash,progress,user,MTP_COPY_LOCAL);}
    if(!rc && memcmp(hash,result->digest,32)) rc=MTP_VERIFY;if(rc) goto done;
    rc=ops_native_check(p->source.location.root,e->path,&e->native,1);if(rc)goto done;
    rc=ops_cancel(progress,user,MTP_COPY_PUBLISH,source.size,source.size);if(!rc) rc=sink->publish(sink->user,&result->published);
done:
    if(fd>=0 && close(fd)) rc=MTP_FILESYSTEM;free(data);
    if(started) {int clean=sink->dispose(sink->user);if(clean) {result->cleanup_failed=1;if(!rc) rc=clean;}}
    return rc;
}

static int ops_upload_parent(psmtp_op_plan *p,mtp_client *c) {
    mtp_object_info info;int rc;if(p->phone_parent==MTP_ROOT)return 0;
    rc=mtp_get_object_info(c,p->phone_parent,&info);
    return rc?rc:!ops_info_same(&info,&p->phone_parent_info,0)?MTP_VERIFY:0;
}
int psmtp_op_space(psmtp_op_plan *p,mtp_client *c) {
    if(p->destination_location.kind==PSMTP_LOCATION_PHONE){
        if(!c || !c->session || c->poisoned)return MTP_STATE;
        mtp_storage_info *st=malloc(sizeof(*st));int rc;if(!st)return MTP_MEMORY;
        rc=mtp_get_storage_info(c,p->phone_storage,st);
        if(!rc && st->access)rc=MTP_UNSUPPORTED;
        if(!rc && (st->free_bytes==UINT64_MAX || st->free_bytes>st->capacity))rc=MTP_SPACE_UNKNOWN;
        if(!rc && (p->bytes>st->free_bytes || (st->free_objects!=UINT32_MAX && p->count>st->free_objects)))rc=MTP_NO_SPACE;
        if(p->log)fprintf(p->log,"stage=phone-space status=%d required=%" PRIu64 " available=%" PRIu64 "\n",rc,p->bytes,rc?0:st->free_bytes);
        free(st);return rc;
    }
    return psmtp_space_require(p->destination_location.root,p->destination,p->bytes,p->log);
}
static int ops_upload_capabilities(const psmtp_op_plan *p,const mtp_device_info *device) {
    size_t i;
    if(!device->identity_valid || !mtp_supports(device,0x100c) || !mtp_supports(device,0x100d) || !mtp_supports(device,0x100b) || (!mtp_supports(device,0x101b) && !mtp_supports(device,0x95c1)))return MTP_UNSUPPORTED;
    for(i=0;i<p->count;++i)if(!p->nodes[i].folder){uint64_t size=p->nodes[i].phone.size;
        if(size>MTP_UPLOAD_MAX_SIZE)return MTP_LIMIT;
        if(size>MTP_UPLOAD_STANDARD_MAX_SIZE && (!mtp_supports(device,0x95c1) || !mtp_supports(device,0x9803)))return MTP_UNSUPPORTED;
    }
    return 0;
}
int psmtp_op_target_phone(psmtp_op_plan *p,mtp_client *c,const psmtp_location *location,uint32_t storage,uint32_t parent) {
    mtp_device_info *device;size_t i;int rc;
    if(!c || !p->count || location->kind!=PSMTP_LOCATION_PHONE || p->source.location.kind==PSMTP_LOCATION_PHONE || !storage || storage==MTP_ROOT || !parent)return MTP_UNSUPPORTED;
    device=malloc(sizeof(*device));if(!device)return MTP_MEMORY;rc=mtp_get_device_info(c,device);
    if(!rc)rc=ops_upload_capabilities(p,device);
    if(!rc){p->destination_location=*location;p->phone_storage=storage;p->phone_parent=parent;memcpy(p->destination_identity,device->identity,32);
        for(i=0;i<p->count;++i)if(!psmtp_name_valid(p->nodes[i].name) || (!p->nodes[i].folder && p->nodes[i].phone.size>MTP_UPLOAD_MAX_SIZE)){rc=MTP_LIMIT;break;}
    }
    free(device);if(rc)return rc;
    if(parent!=MTP_ROOT){rc=mtp_get_object_info(c,parent,&p->phone_parent_info);if(!rc && (!mtp_is_folder(&p->phone_parent_info) || p->phone_parent_info.storage!=storage || p->phone_parent_info.protection))rc=MTP_VERIFY;if(rc)return rc;}
    snprintf(p->destination,sizeof(p->destination),"Phone / %.200s",parent==MTP_ROOT?"":p->phone_parent_info.name);
    return psmtp_op_space(p,c);
}
static int ops_phone_destination(psmtp_op_plan *p,psmtp_op_node *e,mtp_client *c) {
    mtp_object_info info;uint64_t size;int rc;(void)p;rc=mtp_get_object_info(c,e->uploaded_handle,&info);
    if(!rc && !ops_info_same(&e->uploaded_info,&info,!e->folder))rc=MTP_VERIFY;
    if(!rc && !e->folder && e->phone.size>=UINT32_MAX){rc=mtp_get_object_size(c,e->uploaded_handle,&size);if(!rc && size!=e->phone.size)rc=MTP_VERIFY;}
    return rc;
}
static int ops_peer_check(mtp_client *c,uint32_t storage,uint32_t parent,const char *name,uint32_t **peers,size_t *peer_count) {
    uint32_t *ids=malloc(MTP_MAX_ITEMS*sizeof(*ids));mtp_object_info *info=malloc(sizeof(*info));size_t count=0,i;int rc;
    *peers=NULL;*peer_count=0;
    if(!ids || !info){free(ids);free(info);return MTP_MEMORY;}
    rc=mtp_get_handles(c,storage,parent,ids,MTP_MAX_ITEMS,&count);
    for(i=0;!rc && i<count;++i){rc=mtp_get_object_info(c,ids[i],info);if(!rc && (info->storage!=storage || (info->parent!=parent && !(parent==MTP_ROOT && !info->parent))))rc=MTP_VERIFY;
        if(!rc && !psmtp_ui_name_compare(info->name,name))rc=MTP_EXISTS;
    }if(!rc){*peers=ids;*peer_count=count;ids=NULL;}free(ids);free(info);return rc;
}
int psmtp_op_mkdir(const psmtp_location *location,const char *path,mtp_client *c,uint32_t storage,uint32_t parent,const char *name,FILE *log){
    int rc=0;if(!location || !psmtp_name_valid(name))return MTP_STATE;
    if(location->kind==PSMTP_LOCATION_PHONE){
        uint32_t *peers=NULL,handle=0;size_t count=0,i;mtp_object_info info;mtp_storage_info st;mtp_device_info device;
        if(!c || !c->session || c->poisoned)return MTP_STATE;
        rc=mtp_get_device_info(c,&device);if(!rc && !mtp_supports(&device,0x100c))rc=MTP_UNSUPPORTED;
        if(!rc)rc=mtp_get_storage_info(c,storage,&st);if(!rc && st.access)rc=MTP_UNSUPPORTED;
        if(!rc && parent!=MTP_ROOT){rc=mtp_get_object_info(c,parent,&info);if(!rc && (!mtp_is_folder(&info) || info.storage!=storage || info.protection))rc=MTP_VERIFY;}
        if(!rc)rc=ops_peer_check(c,storage,parent,name,&peers,&count);
        if(!rc)rc=mtp_send_object_info(c,storage,parent,name,0,1,&handle);
        if(!rc)for(i=0;i<count;++i)if(handle==peers[i]){c->poisoned=1;rc=MTP_PROTOCOL;break;}
        free(peers);
        if(!rc){rc=mtp_get_object_info(c,handle,&info);if(!rc && (!mtp_is_folder(&info) || strcmp(info.name,name) || info.storage!=storage || (info.parent!=parent && !(parent==MTP_ROOT && !info.parent))))rc=MTP_VERIFY;}
    }else{
        char target[256];int pin=-1,fd=-1;psmtp_native_stat st;
        if(!path || !ops_write_area(path,0))return MTP_UNSUPPORTED;
        rc=ops_join(target,path,name);if(rc)return rc;
        pin=psmtp_native_directory(location->root,path);if(pin<0)return MTP_FILESYSTEM;
        if(mkdir(target,0755))rc=errno==EEXIST?MTP_EXISTS:MTP_FILESYSTEM;
        if(!rc){fd=psmtp_native_directory(location->root,target);if(fd<0)rc=MTP_FILESYSTEM;
            else {rc=psmtp_native_status(fd,&st);if(!rc && !S_ISDIR(st.st_mode))rc=MTP_VERIFY;if(close(fd) && !rc)rc=MTP_FILESYSTEM;}}
        if(!rc && fsync(pin))rc=MTP_FILESYSTEM;if(close(pin) && !rc)rc=MTP_FILESYSTEM;
    }
    if(log)fprintf(log,"stage=new-folder status=%d location=%d names-in-log=0\n",rc,location->kind);return rc;
}

typedef struct {psmtp_op_plan *plan;psmtp_op_node *node;int fd;mtp_copy_progress progress;void *user;psmtp_sha256 hash;uint64_t logged;uint8_t head[4096];size_t head_n;} ops_upload_io;
static int ops_upload_read(void *v,uint8_t *data,size_t n) {
    ops_upload_io *io=v;size_t got=0;
    while(got<n){ssize_t k;do{k=read(io->fd,data+got,n-got);}while(k<0 && errno==EINTR);if(k<=0 || (size_t)k>n-got)return MTP_FILESYSTEM;got+=(size_t)k;}
    if(io->head_n<sizeof(io->head)){size_t k=sizeof(io->head)-io->head_n;if(k>n)k=n;memcpy(io->head+io->head_n,data,k);io->head_n+=k;}
    psmtp_sha256_update(&io->hash,data,n);return 0;
}
static int ops_upload_progress(void *v,uint64_t done,uint64_t total) {ops_upload_io *io=v;
    if(done-io->logged>=UINT64_C(67108864) || done==total){if(io->plan->log)fprintf(io->plan->log,"stage=upload-progress bytes=%" PRIu64 "/%" PRIu64 "\n",done,total);io->logged=done;}
    return ops_cancel(io->progress,io->user,MTP_COPY_RECEIVE,done,total)!=0;
}
static int ops_upload_verify(psmtp_op_plan *p,psmtp_op_node *e,mtp_client *c,int extended,mtp_copy_progress progress,void *user,const uint8_t *head,size_t head_n,int full) {
    uint8_t *data=malloc(MTP_COPY_CHUNK),digest[32];uint64_t offset=0,logged=0;psmtp_sha256 hash;int rc=0;
    if(!data)return MTP_MEMORY;psmtp_sha256_init(&hash);
    while(offset<(full?e->phone.size:head_n)){size_t n=(size_t)(e->phone.size-offset>MTP_COPY_CHUNK?MTP_COPY_CHUNK:e->phone.size-offset),got=0;
        if(!full)n=head_n-(size_t)offset;
        rc=ops_cancel(progress,user,MTP_COPY_LOCAL,offset,e->phone.size);if(rc)break;
        rc=mtp_get_partial(c,e->uploaded_handle,offset,n,extended,data,&got);if(rc)break;if(!got || got>n){rc=MTP_VERIFY;break;}
        if(!full && memcmp(data,head+(size_t)offset,got)){rc=MTP_VERIFY;break;}
        psmtp_sha256_update(&hash,data,got);offset+=got;
        if(offset-logged>=UINT64_C(67108864) || offset==e->phone.size){if(p->log)fprintf(p->log,"stage=upload-verify-progress bytes=%" PRIu64 "/%" PRIu64 " partial64=%d\n",offset,e->phone.size,extended);logged=offset;}
    }
    if(!rc){psmtp_sha256_final(&hash,digest);if(full && memcmp(digest,e->digest,32))rc=MTP_VERIFY;}
    if(!rc)rc=ops_phone_destination(p,e,c);free(data);return rc;
}
static int ops_upload_execute(psmtp_op_plan *p,mtp_client *c,int move,mtp_copy_progress progress,void *user) {
    mtp_device_info *device=malloc(sizeof(*device));size_t i;int rc,standard=0,partial64=0;
    if(!device)return MTP_MEMORY;rc=mtp_get_device_info(c,device);
    if(!rc && (!device->identity_valid || memcmp(device->identity,p->destination_identity,32)))rc=MTP_VERIFY;
    if(!rc)rc=ops_upload_capabilities(p,device);
    if(!rc){standard=mtp_supports(device,0x101b);partial64=mtp_supports(device,0x95c1);}free(device);if(rc)return rc;
    rc=ops_upload_parent(p,c);if(!rc)rc=psmtp_op_space(p,c);if(rc)return rc;

    for(i=0;i<p->count;++i){psmtp_op_node *e=&p->nodes[i];rc=ops_native_check(p->source.location.root,e->path,&e->native,!e->folder);if(rc)return rc;}
    for(i=0;i<p->count;++i){
        psmtp_op_node *e=&p->nodes[i];uint32_t parent=e->parent==SIZE_MAX?p->phone_parent:p->nodes[e->parent].uploaded_handle,*peers=NULL;size_t peer_count=0,k;int fd=-1;ops_upload_io io={0};
        int extended=partial64 && (!standard || e->phone.size>=UINT32_MAX);uint64_t started=c->io.now_ms(c->io.user);
        rc=ops_cancel(progress,user,PSMTP_OP_SCAN,i,p->count);if(rc)break;
        rc=ops_upload_parent(p,c);if(!rc && e->parent!=SIZE_MAX)rc=ops_phone_destination(p,&p->nodes[e->parent],c);if(rc)break;
        rc=ops_peer_check(c,p->phone_storage,parent,e->name,&peers,&peer_count);if(rc)break;
        if(!e->folder){psmtp_native_stat st;fd=ops_open(p->source.location.root,e->path,O_RDONLY);if(fd<0){free(peers);rc=MTP_FILESYSTEM;break;}
            rc=psmtp_native_status(fd,&st);if(!rc && !ops_same(&st,&e->native,1))rc=MTP_VERIFY;if(rc){close(fd);free(peers);break;}}
        if(p->log)fprintf(p->log,"stage=upload-file-begin item=%zu bytes=%" PRIu64 " partial64=%d sentinel=%d names-in-log=0\n",i,e->folder?0:e->phone.size,extended,e->phone.size>MTP_UPLOAD_STANDARD_MAX_SIZE);
        rc=mtp_send_object_info(c,p->phone_storage,parent,e->name,e->folder?0:e->phone.size,e->folder,&e->uploaded_handle);
        if(!rc)for(k=0;k<peer_count;++k)if(peers[k]==e->uploaded_handle){rc=MTP_PROTOCOL;c->poisoned=1;break;}
        free(peers);
        if(!rc){e->created=1;
            if(!e->folder){uint8_t extra;io.plan=p;io.node=e;io.fd=fd;io.progress=progress;io.user=user;psmtp_sha256_init(&io.hash);
                rc=mtp_send_object(c,e->phone.size,ops_upload_read,ops_upload_progress,&io);
                if(!rc && read(fd,&extra,1)!=0)rc=MTP_VERIFY;
                if(!rc){psmtp_native_stat st;rc=psmtp_native_status(fd,&st);if(!rc && !ops_same(&st,&e->native,1))rc=MTP_VERIFY;}
                if(!rc)rc=ops_native_check(p->source.location.root,e->path,&e->native,1);
                psmtp_sha256_final(&io.hash,e->digest);
            }
        }
        if(fd>=0 && close(fd) && !rc)rc=MTP_FILESYSTEM;
        if(!rc){rc=mtp_get_object_info(c,e->uploaded_handle,&e->uploaded_info);
            if(!rc && (e->uploaded_info.storage!=p->phone_storage || (e->uploaded_info.parent!=parent && !(parent==MTP_ROOT && !e->uploaded_info.parent)) || strcmp(e->uploaded_info.name,e->name) || mtp_is_folder(&e->uploaded_info)!=e->folder || (!e->folder && e->uploaded_info.size32!=(e->phone.size>=UINT32_MAX?UINT32_MAX:(uint32_t)e->phone.size))))rc=MTP_VERIFY;
            if(!rc && !e->folder && e->phone.size>=UINT32_MAX)rc=ops_phone_destination(p,e,c);
            if(!rc && !e->folder)rc=ops_upload_verify(p,e,c,extended,progress,user,io.head,io.head_n,move);
        }
        if(rc){if(c->poisoned)p->uncertain_upload=1;break;}
        e->verified=1;if(!e->folder)++p->copied;
        if(p->log && !e->folder){size_t k;uint64_t ended=c->io.now_ms(c->io.user);fprintf(p->log,"stage=upload-file-verified item=%zu bytes=%" PRIu64 " elapsed-ms=%" PRIu64 " sha256=",i,e->phone.size,ended>=started?ended-started:0);for(k=0;k<32;++k)fprintf(p->log,"%02x",e->digest[k]);fprintf(p->log," verification=%s source-removed=0 names-in-log=0\n",move?"full-sha256-reread":"quick-prefix-size");}
    }


    if(rc && !c->poisoned){size_t j;for(j=0;j<p->count;++j){psmtp_op_node *e=&p->nodes[j];
        if(e->created && !e->folder && !e->verified){mtp_object_info info;int clean=mtp_get_object_info(c,e->uploaded_handle,&info);
            if(!clean && e->uploaded_info.storage && ops_info_same(&info,&e->uploaded_info,1) && !info.protection)clean=mtp_delete_object(c,e->uploaded_handle);else if(!clean)clean=MTP_VERIFY;
            if(clean)p->cleanup_failed=1;
        }
    }}
    if(rc && c->poisoned){p->uncertain_upload=1;p->cleanup_failed=1;}
    if(!rc && move)rc=psmtp_op_delete(p,c,1,progress,user);
    return ops_error(p,"upload-end",rc);
}
static int ops_empty_native(const char *path) {
    DIR *d=opendir(path);struct dirent *e;int rc=0;if(!d) return MTP_FILESYSTEM;
    for(;;) {errno=0;e=readdir(d);if(!e) {if(errno) rc=MTP_FILESYSTEM;break;}
        if(!psmtp_dirent_valid(e)) {rc=MTP_PROTOCOL;break;}
        if(strcmp(e->d_name,".") && strcmp(e->d_name,"..")) {rc=MTP_VERIFY;break;}}
    if(closedir(d)) rc=MTP_FILESYSTEM;return rc;
}
int psmtp_op_delete(psmtp_op_plan *p,mtp_client *client,int verified_move,mtp_copy_progress progress,void *user) {
    size_t i;int phone=p->source.location.kind==PSMTP_LOCATION_PHONE,rc=0;
    if(phone && (!client || !p->delete_supported)) return MTP_UNSUPPORTED;
    if(!phone && !ops_write_area(p->source.path,1)) return MTP_UNSUPPORTED;

    for(i=0;i<p->count;++i) {
        psmtp_op_node *e=&p->nodes[i];
        rc=phone?ops_phone_check(client,e,1):ops_native_check(p->source.location.root,e->path,&e->native,!e->folder);if(rc) return ops_error(p,"delete-precheck",rc);
        if(phone && e->phone.info.protection) return MTP_UNSUPPORTED;
        if(verified_move) {
            if(!e->verified) return MTP_VERIFY;
            rc=p->destination_location.kind==PSMTP_LOCATION_PHONE?ops_phone_destination(p,e,client):ops_native_check(p->destination_location.root,e->target,&e->destination,!e->folder);if(rc) return rc;
        }
    }
    for(i=p->count;i>0;--i) {
        psmtp_op_node *e=&p->nodes[i-1];rc=ops_cancel(progress,user,PSMTP_OP_DELETE,p->removed,p->count);if(rc) break;
        rc=phone?ops_phone_check(client,e,!e->folder):ops_native_check(p->source.location.root,e->path,&e->native,!e->folder);if(rc) break;
        if(verified_move && !e->folder){rc=p->destination_location.kind==PSMTP_LOCATION_PHONE?ops_phone_destination(p,e,client):ops_native_check(p->destination_location.root,e->target,&e->destination,1);if(rc)break;}
        if(verified_move && e->folder) {rc=p->destination_location.kind==PSMTP_LOCATION_PHONE?ops_phone_destination(p,e,client):ops_native_check(p->destination_location.root,e->target,&e->destination,0);if(rc) break;}
        if(phone) {
            if(e->folder) {uint32_t ids[1];size_t n=0;rc=mtp_get_handles(client,e->phone.info.storage,e->phone.handle,ids,1,&n);if(!rc && n) rc=MTP_VERIFY;if(rc) break;}
            rc=mtp_delete_object(client,e->phone.handle);if(rc) {p->uncertain_delete=client->poisoned;break;}e->removed=1;++p->removed;
            {mtp_object_info info;int missing=mtp_get_object_info(client,e->phone.handle,&info);if(missing!=MTP_RESPONSE || client->response!=0x2009) {rc=missing?missing:MTP_VERIFY;p->uncertain_delete=client->poisoned;break;}}
        } else {
            if(!ops_write_area(e->path,1)) {rc=MTP_UNSUPPORTED;break;}
            if(e->folder) {rc=ops_empty_native(e->path);if(rc) break;rc=rmdir(e->path)?MTP_FILESYSTEM:0;}
            else rc=unlink(e->path)?MTP_FILESYSTEM:0;
            if(rc) break;e->removed=1;++p->removed;rc=ops_sync_parent(p->source.location.root,e->path);if(rc) break;
        }
    }
    if(!rc && !phone && !p->source.folder && p->nodes[0].removed) {int clean=psmtp_store_remove_file_wrapper(p->source.path,p->log);if(clean) p->cleanup_failed=1;}
    return ops_error(p,"delete-end",rc);
}
int psmtp_op_execute(psmtp_op_plan *p,mtp_client *client,int move,mtp_copy_progress progress,void *user) {
    ops_file f={0};mtp_copy_sink sink;int rc=0,fd=-1;size_t i;
    p->error_stage=NULL;p->error_number=0;
    if(!p->count || !p->destination[0]) return MTP_STATE;
    if(move && (p->source.location.kind==PSMTP_LOCATION_PHONE?!p->delete_supported:!ops_write_area(p->source.path,1))) return MTP_UNSUPPORTED;
    if(p->destination_location.kind==PSMTP_LOCATION_PHONE){if(!client || !client->session || client->poisoned)return MTP_STATE;return ops_upload_execute(p,client,move,progress,user);}
    f.plan=p;f.fd=f.destination_dir=f.parent_dir=-1;
    rc=psmtp_space_require(p->destination_location.root,p->destination,p->bytes,p->log);if(rc)return ops_error(p,"space",rc);

    rc=ops_destination_check(p);if(rc) goto done;
    for(i=0;i<p->count;++i) {psmtp_op_node *e=&p->nodes[i];rc=p->source.location.kind==PSMTP_LOCATION_PHONE?ops_phone_check(client,e,1):ops_native_check(p->source.location.root,e->path,&e->native,!e->folder);if(rc) goto done;}
    for(i=0;i<p->count;++i) if(p->nodes[i].folder) {
        psmtp_op_node *e=&p->nodes[i];rc=ops_cancel(progress,user,PSMTP_OP_SCAN,i,p->count);if(rc) goto done;
        rc=ops_destination_check(p);if(rc) goto done;
        if(e->parent!=SIZE_MAX) {psmtp_op_node *parent=&p->nodes[e->parent];rc=ops_native_check(p->destination_location.root,parent->target,&parent->destination,0);if(rc) goto done;}
        if(mkdir(e->target,0755)) {rc=MTP_FILESYSTEM;goto done;}e->created=1;
        fd=psmtp_native_directory(p->destination_location.root,e->target);if(fd<0) {rc=MTP_FILESYSTEM;goto done;}
        rc=psmtp_native_status(fd,&e->destination);if(fsync(fd)) rc=MTP_FILESYSTEM;if(close(fd)) rc=MTP_FILESYSTEM;fd=-1;if(rc) goto done;
    }
    for(i=0;i<p->count;++i) if(!p->nodes[i].folder) {
        psmtp_op_node *e=&p->nodes[i];mtp_copy_result result;
        f.node=e;f.fd=f.destination_dir=f.parent_dir=-1;f.owned=f.published=0;
        sink.user=&f;sink.begin=ops_file_begin;sink.space=ops_file_space;sink.write=ops_file_write;sink.sync=ops_file_sync;sink.rewind=ops_file_rewind;sink.read=ops_file_read;sink.publish=ops_file_publish;sink.dispose=ops_file_dispose;sink.prepare=NULL;
        rc=p->source.location.kind==PSMTP_LOCATION_PHONE?(move?mtp_copy_run:mtp_copy_run_quick)(client,&e->phone,&sink,progress,user,&result,p->log):ops_local_file(p,e,&sink,progress,user,&result);
        if(result.published) {++p->copied;e->created=1;}
        if(rc || !result.published) {if(!rc) rc=MTP_VERIFY;goto done;}e->verified=1;memcpy(e->digest,result.digest,32);
        if(p->log) {size_t k;fprintf(p->log,"stage=operation-file-verified item=%zu bytes=%" PRIu64 " sha256=",i,e->phone.size);for(k=0;k<32;++k) fprintf(p->log,"%02x",e->digest[k]);fputc('\n',p->log);}
    }
    for(i=p->count;i>0;--i) if(p->nodes[i-1].folder) {rc=ops_native_check(p->destination_location.root,p->nodes[i-1].target,&p->nodes[i-1].destination,0);if(!rc) rc=ops_sync_parent(p->destination_location.root,p->nodes[i-1].target);if(rc) goto done;p->nodes[i-1].verified=1;}
    if(move) rc=psmtp_op_delete(p,client,1,progress,user);
done:
    if(fd>=0 && close(fd) && !rc) rc=MTP_FILESYSTEM;
    if(ops_sync_parent(p->destination_location.root,p->nodes[0].target) && !rc) rc=MTP_FILESYSTEM;
    return ops_error(p,"end",rc);
}
