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
#include "psmtp/diagnostic.h"
#include "screen.h"
#include "exit.h"
#include "psmtp/version.h"
#ifndef PSMTP_PRODUCTION
#include "psmtp/log_paths.h"
#endif
#ifdef PSMTP_TEST_EXPLORER
#include "ps4_explorer_test_api.h"
#else
#include <orbis/libkernel.h>
#include <orbis/Pad.h>
#endif
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <stdlib.h>
#include "psmtp/navigator.h"
#include "copy_file.h"
#include "file_ops.h"
#include "storage_space.h"
#include "process_access.h"
#include "psmtp/device_watch.h"
#include "device_audio.h"
#include "preview.h"
#include "psmtp/name.h"
#include "psmtp/keyboard.h"
#include "psmtp/ui_format.h"
#include "package_install.h"
#include "release_config.h"

static struct {
    diag_log ring;
    FILE *file,*stream,*scan_file;
    size_t scroll;
    int screen_ready,names,pad_failure_reported,drawing;
    int scan_started,scan_failed;
    int copying,view;
    psmtp_locations locations;
    psmtp_ps4_candidate phones[128];size_t phone_count;
    unsigned usb_mask;
    int drawer,exit_requested,refresh_requested,switch_requested,initial_launch;
    int list_view,drawer_settings,open_requested,progress_hidden;
    int toolbar_focus,sort_mode,preview_pause;
    psmtp_location target,paste_location;
    psmtp_op_ref clipboard;int clipboard_active,clipboard_cut,paste_pending;
    char paste_path[256],resume_path[256],clipboard_label[320];psmtp_native_stat paste_stat;
    psmtp_local_browser *local_current;mtp_navigator *phone_current,*retained_phone;
    psmtp_location retained_location;int monitor_live,poll_failed,phone_detached,phone_dirty;
    char last_name[MTP_STRING_BYTES],last_result[128];
    char copy_status[128];
    uint64_t last_draw,mount_probe,loading_started,folder_poll,folder_metadata_poll;
    char status[128];
    char scan_status[80];
} app;

static int poll_devices(int);
static void activate_local(psmtp_local_browser *);
static void local_step(psmtp_local_browser *,uint32_t);
static void internal_fallback(psmtp_local_browser *,const char *);

static void transfer_warning(int);
static int rename_available(void);
static int paste_available(void);
static void new_folder(void);
static void draw_ui(const diag_log *log,size_t offset,const char *status,int names,const char *save,const psmtp_console_selection *panel,const mtp_navigator *nav) {
    psmtp_console_selection visible={0};


    if(app.drawing) return;
    app.drawing=1;
    if(panel) visible=*panel;else visible.view=app.view;
    visible.locations=&app.locations;visible.drawer_focus=app.drawer;
    visible.list_view=app.list_view;visible.drawer_settings=app.drawer_settings;
    visible.browser=app.phone_current;
    if(!visible.local && app.view==PSMTP_VIEW_LOCAL) visible.local=app.local_current;
    if(app.clipboard_active) {snprintf(app.clipboard_label,sizeof(app.clipboard_label),"%s: %.255s",app.clipboard_cut?"Cut":"Copy",app.clipboard.name);visible.clipboard_label=app.clipboard_label;}
    visible.can_select=(app.phone_current && mtp_nav_selected(app.phone_current)) || (app.view==PSMTP_VIEW_LOCAL && app.local_current && app.local_current->count);
    visible.can_paste=paste_available();
    visible.can_create=app.phone_current || (app.view==PSMTP_VIEW_LOCAL && app.local_current && !app.local_current->error);
    visible.toolbar_focus=app.toolbar_focus;visible.sort_mode=app.sort_mode;visible.rename_allowed=visible.can_select && rename_available();
    {struct timespec ts={0};uint64_t now;clock_gettime(CLOCK_MONOTONIC,&ts);now=(uint64_t)ts.tv_sec*1000+(uint64_t)ts.tv_nsec/1000000;
    psmtp_preview_frame(&app.target,app.phone_current,app.view==PSMTP_VIEW_LOCAL?app.local_current:NULL,app.list_view,!app.preview_pause && !app.copying && (visible.view==PSMTP_VIEW_PHONE || visible.view==PSMTP_VIEW_LOCAL || visible.view==PSMTP_VIEW_INFO),now);}
    visible.metadata=psmtp_preview_lookup;
    psmtp_screen_draw(log,offset,status,names,save,&visible,nav);
    app.drawing=0;
}

#ifndef PSMTP_PRODUCTION
static FILE *open_log(const char *path,const char *mode) {
    if(mkdir(PSMTP_LOG_DIRECTORY,0755) && errno!=EEXIST) return NULL;


    return fopen(path,mode);
}



static void scan_line(void *unused,const char *line,size_t length) {
    (void)unused;
    if(!app.scan_file || app.scan_failed) return;
    if(fwrite(line,1,length,app.scan_file)!=length || fputc('\n',app.scan_file)==EOF) {
        app.scan_failed=1;
        strcpy(app.scan_status,"WRITE FAILED - scan file incomplete");
    }
}
static void scan_log_begin(void) {
    int saved_errno;
    diag_log_finish_line(&app.ring);
    app.scan_failed=0;


    app.scan_file=open_log(PSMTP_SCAN_LOG_PATH,app.scan_started?"a":"w");
    saved_errno=errno;
    if(!app.scan_file) {
        app.scan_failed=1;
        snprintf(app.scan_status,sizeof(app.scan_status),"UNAVAILABLE (errno=%d)",saved_errno);
        fprintf(app.stream,"Scan log unavailable errno=%d; general log still attempted.\n",saved_errno);
        return;
    }
    app.scan_started=1;
    if(setvbuf(app.scan_file,NULL,_IONBF,0)) {
        app.scan_failed=1;
        strcpy(app.scan_status,"UNAVAILABLE - cannot enable live saving");
        fclose(app.scan_file); app.scan_file=NULL;
        return;
    }
    strcpy(app.scan_status,"SAVING");
    diag_log_set_sink(&app.ring,scan_line,NULL);
}
static void scan_log_end(void) {
    int failed_before=app.scan_failed;
    diag_log_finish_line(&app.ring);
    diag_log_set_sink(&app.ring,NULL,NULL);
    if(app.scan_file) {
        if(fflush(app.scan_file)) app.scan_failed=1;
        if(fsync(fileno(app.scan_file))) app.scan_failed=1;
        if(fclose(app.scan_file)) app.scan_failed=1;
        app.scan_file=NULL;
    }
    if(!app.scan_failed) strcpy(app.scan_status,"SAVED");
    else if(!failed_before) strcpy(app.scan_status,"SAVE FAILED - flush/sync/close error");
}
static ssize_t log_write(void *unused,const char *p,size_t n) {
    (void)unused;
    diag_log_write(&app.ring,p,n);
    if(app.file && fwrite(p,1,n,app.file)!=n) {
        const char message[]="\nLOG FILE WRITE FAILED; photograph TV results.\n";
        fclose(app.file); app.file=NULL;
        diag_log_write(&app.ring,message,sizeof(message)-1);
    }
    if(app.screen_ready && !app.drawing && !app.copying && app.view==PSMTP_VIEW_LOGS && memchr(p,'\n',n)) {
        struct timespec ts={0}; uint64_t now;
        const char *line=app.ring.current?diag_log_line(&app.ring,app.ring.current-1):NULL;
        now=clock_gettime(CLOCK_MONOTONIC,&ts)?app.last_draw+200:
            (uint64_t)ts.tv_sec*1000+(uint64_t)ts.tv_nsec/1000000;
        if(now-app.last_draw>=200 || (line && strstr(line," begin"))) {
            psmtp_console_selection panel={0};panel.view=PSMTP_VIEW_LOGS;
            app.last_draw=now;
            draw_ui(&app.ring,app.scroll,app.status,app.names,app.scan_status,&panel,NULL);
        }
    }
    return (ssize_t)n;
}
#else
static void scan_log_begin(void) {}
static void scan_log_end(void) {}
#endif
#ifndef PSMTP_PRODUCTION
static void scroll(uint32_t buttons) {
    uint64_t n=diag_log_end(&app.ring)-app.ring.first;
    if(buttons&ORBIS_PAD_BUTTON_UP) ++app.scroll;
    if(buttons&ORBIS_PAD_BUTTON_L1) app.scroll+=20;
    if(app.scroll>n) app.scroll=(size_t)n;
    if(buttons&ORBIS_PAD_BUTTON_DOWN) { if(app.scroll) --app.scroll; }
    if(buttons&ORBIS_PAD_BUTTON_R1) app.scroll=app.scroll>20?app.scroll-20:0;
}
#endif
static void preview_quiet(void) {
    psmtp_preview_cancel();app.preview_pause=1;
    while(psmtp_preview_phone_busy()){psmtp_console_selection s={0};s.view=app.view;draw_ui(&app.ring,0,"Finishing preview read...",1,app.scan_status,&s,NULL);sceKernelUsleep(1000);}
    app.preview_pause=0;
}
static uint32_t read_buttons(void) {
    uint32_t buttons=psmtp_screen_buttons();
    if(buttons)preview_quiet();
    if(psmtp_screen_pad_error() && !app.pad_failure_reported) {
        app.pad_failure_reported=1;
        fprintf(app.stream,"stage=pad-read rc=0x%08" PRIx32 "; controller unavailable, use PS button to leave.\n",(uint32_t)psmtp_screen_pad_error());
    }
    return buttons;
}
static void devices(void *unused,const psmtp_ps4_candidate *phones,size_t n) {
    unsigned usb;(void)unused;if(n>128) n=128;
    usb=psmtp_local_usb_scan(app.stream);
    if(app.monitor_live || n) psmtp_audio_notify(psmtp_device_changes(app.phones,app.phone_count,app.usb_mask,phones,n,usb),app.stream);
    app.monitor_live=1;
    memcpy(app.phones,phones,n*sizeof(*phones));app.phone_count=n;
    app.usb_mask=usb;psmtp_locations_build(&app.locations,phones,n,app.usb_mask);
    fprintf(app.stream,"stage=locations phones-scanned=%zu usb-mask=%02x locations=%zu names-in-log=0\n",n,app.usb_mask,app.locations.count);
}
static int choose(void *unused,const psmtp_ps4_candidate *phones,size_t n) {
    int index;size_t i;(void)unused;
    if(app.initial_launch) {
        for(i=0;i<app.locations.count;++i) if(app.locations.entries[i].kind==PSMTP_LOCATION_PHONE) {
            app.locations.active=app.locations.selected=i;app.target=app.locations.entries[i];break;
        }
    }
    for(i=0;i<app.locations.count;++i) if(psmtp_location_same(&app.target,&app.locations.entries[i])) {app.locations.active=app.locations.selected=i;break;}
    index=psmtp_location_phone_index(&app.target,phones,n);
    if(index<0) {fputs("stage=auto-phone unavailable; requested device identity no longer matches\n",app.stream);return -1;}
    fprintf(app.stream,"stage=auto-phone index=%d vid=%04x pid=%04x; no picker confirmation\n",index,app.target.vendor_id,app.target.product_id);
    return index;
}

typedef struct { psmtp_console_selection panel; uint64_t drawn,started; int timing_phase; } copy_ui;
static void draw_progress(copy_ui *ui,uint64_t now,const char *message) {
    psmtp_console_selection hidden={0};
    if(!ui->started || ui->timing_phase!=ui->panel.phase+1) {ui->started=now;ui->timing_phase=ui->panel.phase+1;}
    ui->panel.elapsed_ms=now-ui->started;
    psmtp_transfer_measure(&ui->panel.transfer,ui->panel.phase,ui->panel.done,ui->panel.total,now);
    if(app.progress_hidden) {hidden.view=app.view;hidden.transfer_active=1;draw_ui(&app.ring,0,"Transfer continues. X shows progress; Circle cancels.",1,app.scan_status,&hidden,app.phone_current);}
    else draw_ui(&app.ring,0,message,1,app.scan_status,&ui->panel,NULL);
}
static int copy_progress(void *v,enum mtp_copy_phase phase,uint64_t done,uint64_t total) {
    copy_ui *ui=(copy_ui *)v; uint32_t buttons=read_buttons(); struct timespec ts={0}; uint64_t now;
    if(psmtp_screen_error() || !psmtp_screen_pad_connected() || (buttons&(ORBIS_PAD_BUTTON_CIRCLE|ORBIS_PAD_BUTTON_OPTIONS))) return 1;
    if(buttons&ORBIS_PAD_BUTTON_CROSS) app.progress_hidden=!app.progress_hidden;
    now=clock_gettime(CLOCK_MONOTONIC,&ts)?ui->drawn+100:(uint64_t)ts.tv_sec*1000+(uint64_t)ts.tv_nsec/1000000;
    if(ui->panel.phase!=(int)phase || ((phase!=MTP_COPY_LOCAL && phase!=MTP_COPY_SOURCE && phase!=MTP_COPY_PUBLISH) && now-ui->drawn>=100) || done==total || (buttons&ORBIS_PAD_BUTTON_CROSS)) {
        ui->panel.copy_mode=2; ui->panel.phase=phase; ui->panel.done=done; ui->panel.total=total;
        draw_progress(ui,now,phase==MTP_COPY_LOCAL?"Verifying...":phase==MTP_COPY_PREPARE?"Preparing disk capacity; Circle cancels.":"Copying selected items; Circle cancels.");
        ui->drawn=now;
    }
    return psmtp_screen_error()!=0;
}
static void copy_checkpoint(void) {
    diag_log_finish_line(&app.ring);
    if(app.scan_file && !app.scan_failed) {
        if(fflush(app.scan_file) || fsync(fileno(app.scan_file))) {app.scan_failed=1;strcpy(app.scan_status,"SAVE FAILED");}
        else strcpy(app.scan_status,"SAVED / browsing");
    }
}
static void local_result(int rc,const psmtp_local_browser *local) {
    fprintf(app.stream,"stage=local-browser status=%d errno=%d failed-at=%s displayed=%zu skipped=%zu truncated=%d location-kind=%d names-in-log=0\n",rc,local->error,local->error_stage?local->error_stage:"none",local->count,local->skipped,local->truncated,app.target.kind);
    copy_checkpoint();
}
#ifndef PSMTP_PRODUCTION
static void diagnostics(void) {
    psmtp_console_selection panel={0};uint32_t buttons;int previous=app.view;
    panel.view=PSMTP_VIEW_LOGS;app.view=PSMTP_VIEW_LOGS;(void)psmtp_screen_buttons();
    for(;;) {
        if(app.monitor_live) (void)poll_devices(0);
        draw_ui(&app.ring,app.scroll,"Diagnostics saved in /data/psmtp. Circle returns to the explorer.",1,app.scan_status,&panel,NULL);
        if(psmtp_screen_error() || !psmtp_screen_pad_ready()) break;
        buttons=read_buttons();if(buttons&(ORBIS_PAD_BUTTON_CIRCLE|ORBIS_PAD_BUTTON_OPTIONS)) break;
        scroll(buttons);sceKernelUsleep(1000);
    }
    app.view=previous;(void)psmtp_screen_buttons();
}
#endif
static void file_action(size_t);
static void rename_selection(void);
static int rename_available(void){
    if(app.phone_current)return app.phone_current->rename_supported && mtp_nav_selected(app.phone_current)!=NULL;
    if(app.local_current && app.local_current->count){const char *p=app.local_current->entries[app.local_current->selected].path;
        if(!strncmp(p,"/mnt/usb",8) && strlen(p)>10)return 1;
        if(!strncmp(p,"/data/",6) && strncmp(p,"/data/psmtp",11))return 1;
        if(!strncmp(p,"/data/psmtp/copies/",18))return 1;
    }return 0;
}
static int paste_available(void){return app.clipboard_active && ((app.phone_current && app.clipboard.location.kind!=PSMTP_LOCATION_PHONE) || (app.view==PSMTP_VIEW_LOCAL && app.local_current && !app.local_current->error));}
static void properties(void) {
    psmtp_console_selection info={0};uint32_t buttons;const mtp_nav_entry *entry;char *full_path=NULL;size_t path_capacity=(MTP_NAV_DEPTH+2)*MTP_STRING_BYTES;
    info.view=PSMTP_VIEW_INFO;
    if(app.phone_current) {
        entry=mtp_nav_selected(app.phone_current);if(!entry) return;
        full_path=malloc(path_capacity);if(!full_path){strcpy(app.status,"Not enough memory for Properties.");return;}
        info.name=entry->info.name;info.modified=entry->info.modified;info.folder=mtp_is_folder(&entry->info);
        {size_t i,used=0;mtp_navigator *nav=app.phone_current;full_path[0]=0;
        for(i=1;i<nav->depth;++i)used+=(size_t)snprintf(full_path+used,path_capacity-used,"/%s",nav->history[i].folder);
        if(nav->depth)used+=(size_t)snprintf(full_path+used,path_capacity-used,"/%s",nav->folder_name);
        snprintf(full_path+used,path_capacity-used,"/%s",entry->info.name);info.path=full_path;}
        info.source_total=entry->exact_size?entry->bytes:entry->info.size32;info.size_unknown=entry->info.size32==UINT32_MAX && !entry->exact_size;
    } else if(app.local_current && app.local_current->count) {
        const psmtp_local_entry *item=&app.local_current->entries[app.local_current->selected];info.name=item->name;info.modified=item->modified;info.folder=item->folder;info.source_total=item->bytes;info.local=app.local_current;info.path=item->path;
    } else return;
    (void)psmtp_screen_buttons();
    for(;;) {
        if(app.monitor_live) (void)poll_devices(0);
        if(app.phone_detached || (app.target.kind==PSMTP_LOCATION_USB && !(app.usb_mask&(1u<<app.target.slot)))) break;
        draw_ui(&app.ring,0,"Properties",1,app.scan_status,&info,NULL);
        if(psmtp_screen_error() || !psmtp_screen_pad_ready()) break;
        buttons=read_buttons();if(buttons&(ORBIS_PAD_BUTTON_CIRCLE|ORBIS_PAD_BUTTON_OPTIONS|ORBIS_PAD_BUTTON_CROSS)) break;
        sceKernelUsleep(1000);
    }
    (void)psmtp_screen_buttons();
    free(full_path);
}
static void refresh_folder(void) {
    int rc=MTP_STATE;preview_quiet();psmtp_preview_invalidate();
    if(app.phone_current) rc=mtp_nav_refresh(app.phone_current);
    else if(app.local_current) {char path[256];strcpy(path,app.local_current->path);rc=psmtp_local_load(app.local_current,path);local_result(rc,app.local_current);}
    strcpy(app.status,rc?"Folder refresh failed. Check the device and retry.":"Folder refreshed.");
}

static void idle_refresh(void){
    struct timespec ts;uint64_t now;int changed=0,rc=0;
    if(app.copying || app.preview_pause || app.drawer || app.toolbar_focus || app.switch_requested || psmtp_preview_phone_busy() || clock_gettime(CLOCK_MONOTONIC,&ts))return;
    now=(uint64_t)ts.tv_sec*1000+(uint64_t)ts.tv_nsec/1000000;if(now-app.folder_poll<1500)return;app.folder_poll=now;
    if(app.phone_current){mtp_navigator *nav=app.phone_current;uint32_t *ids;size_t n=0,i,j;
        if(nav->client->poisoned)return;ids=malloc(MTP_MAX_ITEMS*sizeof(*ids));if(!ids)return;
        rc=mtp_get_handles(nav->client,nav->storages[nav->storage_index],nav->parent,ids,MTP_MAX_ITEMS,&n);
        if(!rc){changed=n!=nav->count;for(i=0;!changed && i<n;++i){for(j=0;j<nav->count && ids[i]!=nav->handles[j];++j){}if(j==nav->count)changed=1;}}
        free(ids);
        if(!rc && !changed && now-app.folder_metadata_poll>=6000){
            app.folder_metadata_poll=now;for(i=0;i<nav->page_count && !changed;++i){mtp_object_info info;mtp_nav_entry *e=&nav->entries[i];
                rc=mtp_get_object_info(nav->client,e->handle,&info);if(rc)break;
                changed=strcmp(info.name,e->info.name) || strcmp(info.modified,e->info.modified) || info.size32!=e->info.size32 || info.format!=e->info.format;
            }
        }
        if(!rc && changed){app.preview_pause=1;rc=mtp_nav_refresh(nav);app.preview_pause=0;}
    }else if(app.view==PSMTP_VIEW_LOCAL && app.local_current){
        psmtp_local_browser *old=app.local_current,*next=calloc(1,sizeof(*next));size_t i;char selected[256]="";
        if(!next)return;if(old->count)snprintf(selected,sizeof(selected),"%s",old->entries[old->selected].path);
        snprintf(next->root,sizeof(next->root),"%s",old->root);next->sort_mode=old->sort_mode;rc=psmtp_local_load(next,old->path);
        if(!rc){changed=old->count!=next->count || old->error;
            for(i=0;!changed && i<next->count;++i){psmtp_local_entry *a=&old->entries[i],*b=&next->entries[i];changed=strcmp(a->path,b->path) || strcmp(a->name,b->name) || strcmp(a->modified,b->modified) || a->folder!=b->folder || a->bytes!=b->bytes || a->verified!=b->verified || a->incomplete!=b->incomplete;}
            if(changed){for(i=0;i<next->count;++i)if(!strcmp(next->entries[i].path,selected)){next->selected=i;break;}
                next->scan_progress=old->scan_progress;next->scan_user=old->scan_user;*old=*next;}}
        free(next);
    }
    if(!rc && changed){psmtp_preview_invalidate();fprintf(app.stream,"stage=directory-auto-refresh location=%d changed=1\n",app.target.kind);}
}
static void sort_picker(void){
    psmtp_console_selection s={0};s.view=PSMTP_VIEW_SORT;s.index=(size_t)app.sort_mode;(void)psmtp_screen_buttons();
    for(;;){uint32_t buttons;int rc=0;if(app.monitor_live)(void)poll_devices(0);draw_ui(&app.ring,0,"Choose a sort order.",1,app.scan_status,&s,NULL);
        if(psmtp_screen_error() || !psmtp_screen_pad_ready() || app.phone_detached)break;buttons=read_buttons();if(buttons&(ORBIS_PAD_BUTTON_CIRCLE|ORBIS_PAD_BUTTON_OPTIONS))break;
        if((buttons&ORBIS_PAD_BUTTON_UP) && s.index)--s.index;if((buttons&ORBIS_PAD_BUTTON_DOWN) && s.index+1<PSMTP_SORT_COUNT)++s.index;
        if(buttons&ORBIS_PAD_BUTTON_CROSS){if(app.phone_current)rc=mtp_nav_sort(app.phone_current,(int)s.index);else if(app.local_current)psmtp_local_sort(app.local_current,(int)s.index);if(!rc)app.sort_mode=(int)s.index;else strcpy(app.status,"Could not sort this folder. Refresh and retry.");break;}sceKernelUsleep(1000);
    }(void)psmtp_screen_buttons();
}
static void explorer_menu(int settings) {
    psmtp_console_selection menu={0};uint32_t buttons;if(settings)return;menu.view=settings?PSMTP_VIEW_SETTINGS:PSMTP_VIEW_MENU;(void)psmtp_screen_buttons();
    for(;;) {
        size_t count=PSMTP_MENU_COUNT;
        if(app.monitor_live) (void)poll_devices(0);
        if(app.phone_current && app.phone_detached && !settings) break;
        if(menu.index>=count) menu.index=count-1;
        draw_ui(&app.ring,0,settings?app.status:"Options",1,app.scan_status,&menu,NULL);
        if(psmtp_screen_error() || !psmtp_screen_pad_ready()) break;
        buttons=read_buttons();if(buttons&(ORBIS_PAD_BUTTON_CIRCLE|ORBIS_PAD_BUTTON_OPTIONS|ORBIS_PAD_BUTTON_TRIANGLE)) break;
        if((buttons&ORBIS_PAD_BUTTON_UP) && menu.index) --menu.index;
        if((buttons&ORBIS_PAD_BUTTON_DOWN) && menu.index+1<count) ++menu.index;
        if(buttons&ORBIS_PAD_BUTTON_CROSS) {
                if(menu.index==PSMTP_MENU_EXIT){app.exit_requested=1;break;}
                if(menu.index==PSMTP_MENU_NEW_FOLDER){new_folder();break;}
                if(menu.index==PSMTP_MENU_PASTE){if(!paste_available())continue;file_action(2);break;}
                if(menu.index==PSMTP_MENU_RENAME){if(!rename_available())continue;rename_selection();break;}
                if(!((app.phone_current && mtp_nav_selected(app.phone_current)) || (app.view==PSMTP_VIEW_LOCAL && app.local_current && app.local_current->count)))continue;
                if(menu.index==PSMTP_MENU_OPEN){app.open_requested=1;app.drawer=app.drawer_settings=0;}
                else if(menu.index==PSMTP_MENU_COPY)file_action(0);
                else if(menu.index==PSMTP_MENU_MOVE)file_action(1);
                else if(menu.index==PSMTP_MENU_DELETE)file_action(3);
                else if(menu.index==PSMTP_MENU_PROPERTIES)properties();
            break;
        }
        sceKernelUsleep(1000);
    }
    (void)psmtp_screen_buttons();
}
static size_t selected_index(void) {return app.phone_current?app.phone_current->selected:app.local_current?app.local_current->selected:0;}
static uint32_t file_buttons(void) {uint32_t buttons=read_buttons();if(app.open_requested) {app.open_requested=0;buttons|=ORBIS_PAD_BUTTON_CROSS;}return buttons;}
static int shared_controls(uint32_t buttons) {
    if(!app.drawer && app.toolbar_focus){
        if(buttons&(ORBIS_PAD_BUTTON_DOWN|ORBIS_PAD_BUTTON_CIRCLE)){app.toolbar_focus=0;return 1;}
        if(buttons&ORBIS_PAD_BUTTON_LEFT){if(app.toolbar_focus>1)--app.toolbar_focus;else {app.toolbar_focus=0;app.drawer=1;app.locations.selected=app.locations.active;}return 1;}
        if(buttons&ORBIS_PAD_BUTTON_RIGHT){if(app.toolbar_focus<3)++app.toolbar_focus;return 1;}
        if(buttons&ORBIS_PAD_BUTTON_CROSS){if(app.toolbar_focus<3)app.list_view=app.toolbar_focus==2;else sort_picker();return 1;}
        return 1;
    }
    if(!app.drawer && (buttons&ORBIS_PAD_BUTTON_UP) && selected_index()<(app.list_view?1u:4u)){app.toolbar_focus=app.list_view?2:1;return 1;}
    if(buttons&ORBIS_PAD_BUTTON_L3) {app.list_view=!app.list_view;return 1;}
    if(buttons&ORBIS_PAD_BUTTON_OPTIONS) {explorer_menu(app.drawer && app.drawer_settings);return 1;}
    if(!app.drawer && (buttons&ORBIS_PAD_BUTTON_TRIANGLE)) {explorer_menu(0);return 1;}
    if(buttons&ORBIS_PAD_BUTTON_TOUCH_PAD) {if(app.drawer) app.refresh_requested=1;else refresh_folder();return 1;}
    if((buttons&ORBIS_PAD_BUTTON_LEFT) && !app.drawer && (app.list_view || selected_index()%4==0)) {app.drawer=1;app.drawer_settings=0;app.locations.selected=app.locations.active;return 1;}
    if(!app.drawer) return 0;
    if(app.drawer_settings && (buttons&(ORBIS_PAD_BUTTON_RIGHT|ORBIS_PAD_BUTTON_CROSS))) return 1;
    if(buttons&(ORBIS_PAD_BUTTON_RIGHT|ORBIS_PAD_BUTTON_CROSS|ORBIS_PAD_BUTTON_CIRCLE)) {app.drawer=app.drawer_settings=0;return 1;}
    if(buttons&ORBIS_PAD_BUTTON_TRIANGLE) app.refresh_requested=1;
    if(buttons&ORBIS_PAD_BUTTON_UP) {if(app.drawer_settings) app.drawer_settings=0;else if(psmtp_locations_move(&app.locations,-1)) app.switch_requested=1;}
    if(buttons&ORBIS_PAD_BUTTON_DOWN) {if(!app.drawer_settings && app.locations.selected+1>=app.locations.count) return 1;else if(!app.drawer_settings && psmtp_locations_move(&app.locations,1)) app.switch_requested=1;}
    if(app.switch_requested) app.target=app.locations.entries[app.locations.selected];
    return 1;
}
static int usb_target_missing(void) {
    return app.target.kind==PSMTP_LOCATION_USB && !(app.usb_mask&(1u<<app.target.slot));
}
static int poll_devices(int force) {
    struct timespec ts={0};uint64_t now;unsigned usb;psmtp_ps4_candidate phones[128]={{0}};size_t n=0;int rc,changed;
    if(app.copying || psmtp_preview_phone_busy() || clock_gettime(CLOCK_MONOTONIC,&ts)) return usb_target_missing();
    now=(uint64_t)ts.tv_sec*1000+(uint64_t)ts.tv_nsec/1000000;
    if(!force && now-app.mount_probe<PSMTP_DEVICE_POLL_MS) return usb_target_missing();
    app.mount_probe=now;rc=psmtp_ps4_poll_devices(phones,&n,force,app.stream);
    psmtp_audio_status(app.stream);
    if(rc) {
        if(!app.poll_failed) fprintf(app.stream,"stage=device-watch failed status=%d; keeping last phone inventory\n",rc);
        n=app.phone_count;memcpy(phones,app.phones,n*sizeof(*phones));
    } else if(app.poll_failed) fputs("stage=device-watch recovered=1\n",app.stream);
    app.poll_failed=rc!=0;
    usb=psmtp_local_usb_mask();
    if(!clock_gettime(CLOCK_MONOTONIC,&ts)) now=(uint64_t)ts.tv_sec*1000+(uint64_t)ts.tv_nsec/1000000;
    changed=usb!=app.usb_mask || n!=app.phone_count || memcmp(phones,app.phones,n*sizeof(*phones));
    if(changed) {
        unsigned events=psmtp_device_changes(app.phones,app.phone_count,app.usb_mask,phones,n,usb);
        psmtp_audio_notify(events,app.stream);
        memcpy(app.phones,phones,n*sizeof(*phones));app.phone_count=n;app.usb_mask=usb;
        psmtp_locations_build(&app.locations,phones,n,usb);
        fprintf(app.stream,"stage=device-watch changed=1 timestamp-ms=%" PRIu64 " devices=%zu usb-mask=%02x interval-ms=%u\n",now,app.locations.count,usb,PSMTP_DEVICE_POLL_MS);copy_checkpoint();
    }
    if(app.clipboard_active && app.clipboard.location.kind==PSMTP_LOCATION_PHONE && psmtp_location_phone_index(&app.clipboard.location,phones,n)<0) {
        app.clipboard_active=0;strcpy(app.status,"Phone disconnected. Copy or cut an item again after reconnecting.");
        fputs("stage=clipboard-disconnected cleared=1\n",app.stream);
    }
    if(app.retained_phone && !app.phone_detached && psmtp_location_phone_index(&app.retained_location,phones,n)<0) {
        app.retained_phone->client->poisoned=1;app.phone_detached=1;
        if(app.clipboard_active && psmtp_location_same(&app.clipboard.location,&app.retained_location)) app.clipboard_active=0;
        fputs("stage=phone-detached idle=1 session-disarmed=1\n",app.stream);copy_checkpoint();
    }
    return usb_target_missing();
}
static int catalog_progress(void *unused,size_t done,size_t total) {
    psmtp_console_selection panel={0};struct timespec ts={0};uint64_t now;uint32_t buttons=read_buttons();
    if(psmtp_screen_error() || !psmtp_screen_pad_connected() || (buttons&(ORBIS_PAD_BUTTON_CIRCLE|ORBIS_PAD_BUTTON_OPTIONS))) return 1;
    now=clock_gettime(CLOCK_MONOTONIC,&ts)?app.last_draw+100:(uint64_t)ts.tv_sec*1000+(uint64_t)ts.tv_nsec/1000000;
    if(!done) app.loading_started=now;
    if(now-app.loading_started<180) return 0;
    if(done==total || now-app.last_draw>=100) {
        panel.view=PSMTP_VIEW_LOADING;panel.done=done;panel.total=total;
        draw_ui(&app.ring,0,unused?"Reading device directory. Circle cancels.":"Reading phone metadata. Circle cancels.",1,app.scan_status,&panel,NULL);app.last_draw=now;
    }
    return 0;
}
static int capture_selection(psmtp_op_ref *source) {
    const mtp_nav_entry *entry;
    if(app.phone_current) {
        entry=mtp_nav_selected(app.phone_current);if(!entry || strlen(entry->info.name)>255) return MTP_STATE;
        memset(source,0,sizeof(*source));source->location=app.target;source->info=entry->info;source->handle=entry->handle;
        strcpy(source->name,entry->info.name);source->folder=mtp_is_folder(&entry->info);source->identity_valid=app.phone_current->identity_valid;
        memcpy(source->phone_identity,app.phone_current->device_identity,32);return 0;
    }
    if(app.view!=PSMTP_VIEW_LOCAL || !app.local_current || !app.local_current->count) return MTP_STATE;
    {int rc=psmtp_op_capture(source,&app.target,&app.local_current->entries[app.local_current->selected]);if(!rc)snprintf(source->directory,sizeof(source->directory),"%s",app.local_current->path);return rc;}
}
static void install_notice(const char *title,const char *text){
    psmtp_console_selection p={0};p.view=PSMTP_VIEW_NOTICE;p.notice_title=title;p.notice_text=text;(void)psmtp_screen_buttons();
    for(;;){draw_ui(&app.ring,0,title,1,app.scan_status,&p,NULL);if(psmtp_screen_error() || !psmtp_screen_pad_ready() || (read_buttons()&(ORBIS_PAD_BUTTON_CROSS|ORBIS_PAD_BUTTON_CIRCLE|ORBIS_PAD_BUTTON_OPTIONS)))break;sceKernelUsleep(1000);}
    (void)psmtp_screen_buttons();
}
static int install_selection(void){
    psmtp_op_ref source;psmtp_pkg_info info;psmtp_install_session *session=NULL;psmtp_install_report report={0};copy_ui ui={0};int rc,cancel=0,close_rc,cleanup_rc=0;char error[192];uint32_t buttons;
    preview_quiet();rc=capture_selection(&source);if(rc)return rc;app.copying=1;app.progress_hidden=0;
    rc=psmtp_install_prepare(&session,&source,app.phone_current?app.phone_current->client:NULL,&info,app.stream);if(rc)goto done;
    ui.panel.view=PSMTP_VIEW_INSTALL_CONFIRM;ui.panel.action=source.location.kind==PSMTP_LOCATION_PHONE?3:0;ui.panel.name=source.name;ui.panel.source_total=info.size;ui.panel.destination=source.location.label;ui.panel.notice_text=info.content_id;
    (void)psmtp_screen_buttons();
    for(;;){draw_ui(&app.ring,0,"Confirm package installation.",1,app.scan_status,&ui.panel,NULL);
        if(psmtp_screen_error() || !psmtp_screen_pad_ready()){rc=MTP_CANCELLED;goto done;}buttons=read_buttons();
        if(buttons&(ORBIS_PAD_BUTTON_CIRCLE|ORBIS_PAD_BUTTON_OPTIONS)){rc=MTP_CANCELLED;goto done;}if(buttons&ORBIS_PAD_BUTTON_CROSS)break;sceKernelUsleep(1000);}
    (void)psmtp_screen_buttons();
    if(source.location.kind==PSMTP_LOCATION_PHONE){
        ui.panel.view=PSMTP_VIEW_TRANSFERS;ui.panel.copy_mode=2;ui.panel.phase=MTP_COPY_RECEIVE;ui.panel.total=info.size;ui.panel.done=0;
        rc=psmtp_install_stage(&session,1,copy_progress,&ui);if(rc)goto done;
        memset(&ui.panel.transfer,0,sizeof(ui.panel.transfer));ui.started=ui.drawn=0;ui.timing_phase=0;app.progress_hidden=0;
    }
    ui.panel.view=PSMTP_VIEW_INSTALL_PROGRESS;ui.panel.copy_mode=2;ui.panel.phase=MTP_COPY_RECEIVE;ui.panel.total=ui.panel.done=0;
    {struct timespec ts={0};clock_gettime(CLOCK_MONOTONIC,&ts);ui.drawn=(uint64_t)ts.tv_sec*1000+(uint64_t)ts.tv_nsec/1000000;draw_progress(&ui,ui.drawn,"Starting system installer...");}
    rc=psmtp_install_begin(session,1);if(rc)goto done;
    for(;;){struct timespec ts={0};uint64_t now;rc=psmtp_install_poll(session,&report);if(rc)break;
        clock_gettime(CLOCK_MONOTONIC,&ts);now=(uint64_t)ts.tv_sec*1000+(uint64_t)ts.tv_nsec/1000000;
        if(now-ui.drawn>=100 || report.complete){ui.panel.done=report.done;ui.panel.total=report.total;ui.panel.install_percent=report.percent;ui.panel.install_known=report.progress_known;ui.panel.install_preparing=report.preparing;draw_progress(&ui,now,"Keep PSMTP open while installing.");ui.drawn=now;}
        if(report.complete)break;buttons=read_buttons();if(psmtp_screen_error() || !psmtp_screen_pad_connected() || (buttons&(ORBIS_PAD_BUTTON_CIRCLE|ORBIS_PAD_BUTTON_OPTIONS))){cancel=1;rc=MTP_CANCELLED;break;}sceKernelUsleep(100000);
    }
done:
    if(!report.native_error)report.native_error=psmtp_install_native_error(session);
    if(!rc && report.complete)cleanup_rc=psmtp_install_cleanup(session);
    close_rc=psmtp_install_close_report(session,cancel || rc!=0,&report.native_error);
    if(close_rc){if(!rc && report.complete)cleanup_rc=close_rc;else rc=close_rc;}
    if(!rc){
        install_notice("Installation successful","PKG installed successfully.");
        strcpy(app.status,"Package installed successfully.");
    }
    else if(!report.native_error && rc==MTP_CANCELLED)strcpy(app.status,"Installation cancelled. Source PKG preserved.");
    else if(report.native_error==0x80990015u || report.native_error==0x80990086u){
        install_notice("Existing installation task","A download for this PKG already exists.\nOpen PS4 Notifications > Downloads and\nremove its failed entry, then retry.\nSource PKG preserved.");
        strcpy(app.status,"Existing task blocks installation. Check PS4 Notifications.");
    }
    else if(!report.native_error && rc==MTP_NO_SPACE){install_notice("Insufficient storage space","Free space for both the full saved PKG\nand the installed content is required.\nSource PKG preserved.");strcpy(app.status,"Not enough PS4 storage for installation. Source PKG preserved.");}
    else {
        if(report.native_error)snprintf(error,sizeof(error),"System installer error (0x%08x).\nCheck PS4 Notifications and available storage.",report.native_error);
        else snprintf(error,sizeof(error),"Could not start installation:\n%s (app error %d).\nSource PKG preserved.",mtp_error_string(rc),rc);
        install_notice("Installation unavailable",error);strcpy(app.status,"Installation unavailable. Source PKG preserved.");
    }
    fprintf(app.stream,"stage=install-selection status=%d native=0x%08x original-source-removed=0 cleanup-warning=%d\n",rc,report.native_error,cleanup_rc);app.copying=0;copy_checkpoint();(void)psmtp_screen_buttons();return rc;
}
static void edit_selection_name(int creating) {
    psmtp_op_ref source;psmtp_console_selection s={0};char name[256],result[256],error[128]="";int rc,shift=0;size_t cursor=0;
    preview_quiet();memset(&source,0,sizeof(source));rc=creating?0:capture_selection(&source);if(rc){strcpy(app.status,"Select an item to rename.");return;}
    snprintf(name,sizeof(name),"%s",creating?"New Folder":source.name);s.view=PSMTP_VIEW_RENAME;s.action=creating?4:0;(void)psmtp_screen_buttons();
    for(;;){uint32_t buttons;size_t n=strlen(name);s.edit_name=name;s.edit_error=error;s.index=cursor;s.keyboard_shift=shift;
        draw_ui(&app.ring,0,creating?"New Folder":"Rename",1,app.scan_status,&s,NULL);if(psmtp_screen_error() || !psmtp_screen_pad_ready())break;
        buttons=read_buttons();if(buttons&ORBIS_PAD_BUTTON_CIRCLE)break;
        if(buttons&ORBIS_PAD_BUTTON_UP)cursor=psmtp_key_move(cursor,0,-1);
        if(buttons&ORBIS_PAD_BUTTON_DOWN)cursor=psmtp_key_move(cursor,0,1);
        if(buttons&ORBIS_PAD_BUTTON_LEFT)cursor=psmtp_key_move(cursor,-1,0);
        if(buttons&ORBIS_PAD_BUTTON_RIGHT)cursor=psmtp_key_move(cursor,1,0);
        if(buttons&ORBIS_PAD_BUTTON_TRIANGLE)shift=!shift;
        if(buttons&ORBIS_PAD_BUTTON_L1){name[0]=0;n=0;}
        if(buttons&ORBIS_PAD_BUTTON_CROSS){int action=psmtp_key_action(cursor);char c=psmtp_key_character(cursor,shift);
            if(action==PSMTP_KEY_SHIFT)shift=!shift;
            else if(action==PSMTP_KEY_CLEAR){name[0]=0;n=0;}
            else if(action==PSMTP_KEY_SAVE)buttons|=ORBIS_PAD_BUTTON_R1;
            else if(action==PSMTP_KEY_BACKSPACE)buttons|=ORBIS_PAD_BUTTON_SQUARE;
            else if(c && n<255){name[n++]=c;name[n]=0;}
        }
        if((buttons&ORBIS_PAD_BUTTON_SQUARE) && n){do{--n;}while(n && ((unsigned char)name[n]&0xc0)==0x80);name[n]=0;}
        if(buttons&ORBIS_PAD_BUTTON_R1){if(!psmtp_name_valid(name)){strcpy(error,"Enter a valid name. Keep the file extension if needed.");continue;}
            if(creating){
                mtp_navigator *nav=app.phone_current;
                rc=psmtp_op_mkdir(&app.target,nav?NULL:app.local_current?app.local_current->path:NULL,nav?nav->client:NULL,nav?nav->storages[nav->storage_index]:0,nav?nav->parent:0,name,app.stream);
                if(rc){snprintf(error,sizeof(error),"%s",rc==MTP_EXISTS?"A file or folder already uses this name.":"Could not create folder. Check storage access and free space.");if(nav && nav->client->poisoned)break;continue;}
                refresh_folder();strcpy(app.status,"Folder created.");break;
            }
            rc=psmtp_op_rename(&source,app.phone_current?app.phone_current->client:NULL,name,result,app.stream);
            if(rc && result[0]){refresh_folder();strcpy(app.status,"Renamed, but the final check failed. Refresh before retrying.");break;}
            if(rc){snprintf(error,sizeof(error),"Rename failed (%d). Name exists, storage is read-only, or device rejected it.",rc);if(app.phone_current && app.phone_current->client->poisoned){strcpy(app.status,"Rename interrupted. Refresh and check the name before retrying.");break;}continue;}
            psmtp_preview_invalidate();if(app.clipboard_active)app.clipboard_active=0;
            refresh_folder();strcpy(app.status,"Renamed successfully.");break;
        }sceKernelUsleep(1000);
    }(void)psmtp_screen_buttons();
}
static void rename_selection(void){edit_selection_name(0);}
static void new_folder(void){edit_selection_name(1);}
static int operation_progress(void *context,enum mtp_copy_phase phase,uint64_t done,uint64_t total) {
    copy_ui *ui=context;struct timespec ts={0};uint64_t now;uint32_t buttons=read_buttons();
    if(psmtp_screen_error() || !psmtp_screen_pad_connected() || (buttons&(ORBIS_PAD_BUTTON_CIRCLE|ORBIS_PAD_BUTTON_OPTIONS))) return 1;
    if(buttons&ORBIS_PAD_BUTTON_CROSS) app.progress_hidden=!app.progress_hidden;
    now=clock_gettime(CLOCK_MONOTONIC,&ts)?ui->drawn+100:(uint64_t)ts.tv_sec*1000+(uint64_t)ts.tv_nsec/1000000;
    if(ui->panel.phase!=(int)phase || ((phase!=MTP_COPY_LOCAL && phase!=MTP_COPY_SOURCE && phase!=MTP_COPY_PUBLISH) && now-ui->drawn>=100) || done==total || (buttons&ORBIS_PAD_BUTTON_CROSS)) {
        ui->panel.copy_mode=2;ui->panel.phase=phase;ui->panel.done=done;ui->panel.total=total;
        draw_progress(ui,now,phase==MTP_COPY_LOCAL?"Verifying...":"Working on your selection. Circle cancels between completed operations.");ui->drawn=now;
    }
    return 0;
}
static void transfer_warning(int rc) {
    psmtp_console_selection s={0};s.view=PSMTP_VIEW_NOTICE;
    s.notice_title=rc==MTP_NO_SPACE?"Insufficient storage space":"Cannot check storage space";
    s.notice_text=rc==MTP_NO_SPACE?"The destination has too little free space.\nFree up space or choose another drive.":"The destination could not report its free space.\nNo transfer was started.\nChoose another drive or reconnect the device.";
    (void)psmtp_screen_buttons();
    for(;;){draw_ui(&app.ring,0,"Transfer has not started.",1,app.scan_status,&s,NULL);
        if(psmtp_screen_error() || !psmtp_screen_pad_ready() || (read_buttons()&(ORBIS_PAD_BUTTON_CROSS|ORBIS_PAD_BUTTON_CIRCLE|ORBIS_PAD_BUTTON_OPTIONS)))break;
        sceKernelUsleep(1000);
    }(void)psmtp_screen_buttons();
}
static int operation_confirm(psmtp_op_plan *plan,copy_ui *ui,int action) {
    uint32_t buttons;ui->panel.view=PSMTP_VIEW_OPERATION;ui->panel.copy_mode=0;ui->panel.action=action;
    ui->panel.files=plan->files;ui->panel.folders=plan->folders;ui->panel.source_total=plan->bytes;ui->panel.destination=plan->destination;
    (void)psmtp_screen_buttons();
    for(;;) {
        draw_ui(&app.ring,0,"Review this operation before confirming.",1,app.scan_status,&ui->panel,NULL);
        if(psmtp_screen_error() || !psmtp_screen_pad_ready()) return MTP_CANCELLED;
        buttons=read_buttons();if(buttons&(ORBIS_PAD_BUTTON_CIRCLE|ORBIS_PAD_BUTTON_OPTIONS)) return MTP_CANCELLED;
        if(buttons&ORBIS_PAD_BUTTON_CROSS) {ui->panel.view=PSMTP_VIEW_PHONE;(void)psmtp_screen_buttons();return 0;}
        sceKernelUsleep(1000);
    }
}
static void run_operation(const psmtp_op_ref *source,mtp_client *client,int action,const psmtp_location *destination,const char *path) {
    psmtp_op_plan *plan=calloc(1,sizeof(*plan));copy_ui ui={0};int rc;
    if(!plan) {strcpy(app.status,"Not enough memory for this operation.");return;}
    app.copying=1;app.progress_hidden=0;ui.panel.name=source->name;ui.panel.folder=source->folder;ui.panel.operation=1;ui.panel.action=action;
    (void)psmtp_screen_buttons();
    rc=psmtp_op_plan_build(plan,source,client,operation_progress,&ui,app.stream);
    if(!rc && action!=2) {
        if(destination->kind==PSMTP_LOCATION_PHONE && app.phone_current)rc=psmtp_op_target_phone(plan,client,destination,app.phone_current->storages[app.phone_current->storage_index],app.phone_current->parent);
        else rc=psmtp_op_target(plan,destination,path);
    }
    if(!rc && app.paste_pending && (app.paste_stat.st_dev!=plan->destination_stat.st_dev || app.paste_stat.st_ino!=plan->destination_stat.st_ino)) rc=MTP_VERIFY;
    if(!rc && action==2 && source->location.kind==PSMTP_LOCATION_PHONE && !plan->delete_supported) rc=MTP_UNSUPPORTED;
    if(!rc && action!=2)rc=psmtp_op_space(plan,client);
    if(rc==MTP_NO_SPACE || rc==MTP_SPACE_UNKNOWN)transfer_warning(rc);
    if(!rc) rc=operation_confirm(plan,&ui,action);
    if(!rc) {
        ui.panel.copy_mode=2;ui.panel.view=PSMTP_VIEW_PHONE;
        rc=action==2?psmtp_op_delete(plan,client,0,operation_progress,&ui):psmtp_op_execute(plan,client,action==1,operation_progress,&ui);
    }
    if(plan->uncertain_upload)strcpy(app.status,"Phone upload interrupted; a partial file may remain. Reconnect and inspect the destination before retrying.");
    else if(plan->uncertain_delete) strcpy(app.status,"Phone deletion interrupted; source state is uncertain. Check both devices before retrying.");
    else if(rc==MTP_CANCELLED) snprintf(app.status,sizeof(app.status),"Cancelled: %zu files copied, %zu source items removed.",plan->copied,plan->removed);
    else if(rc==MTP_NO_SPACE)strcpy(app.status,"Insufficient storage space. Transfer stopped; source items are preserved.");
    else if(rc==MTP_EXISTS)strcpy(app.status,"An item with this name already exists. Rename the source or choose another folder.");
    else if(rc==MTP_UNSUPPORTED)strcpy(app.status,"This device is read-only or does not support the selected operation.");
    else if(rc==MTP_LIMIT)strcpy(app.status,"Selection exceeds supported transfer limits. Choose a smaller file or folder.");
    else if(rc==MTP_SPACE_UNKNOWN)strcpy(app.status,"Available storage could not be checked. Transfer blocked.");
    else if(rc==MTP_FILESYSTEM && plan->error_stage)snprintf(app.status,sizeof(app.status),"Destination access failed (error %d). Check storage access.",plan->error_number);
    else if(rc) snprintf(app.status,sizeof(app.status),"Operation stopped (%d): %zu files copied, %zu items removed.",rc,plan->copied,plan->removed);
    else if(action==2) snprintf(app.status,sizeof(app.status),"Deleted %zu selected items.",plan->removed);
    else snprintf(app.status,sizeof(app.status),action==1?"Move verified: %zu files copied, %zu source items removed.":"Paste complete: %zu files copied and verified.",plan->copied,plan->removed);
    if(plan->cleanup_failed && !rc) strcpy(app.status,"Operation completed. Some temporary files could not be removed.");
    if((action==1 && (!rc || plan->removed || plan->uncertain_delete)) || action==2) app.clipboard_active=0;
    if(client && app.retained_phone && client==app.retained_phone->client && (plan->removed || plan->uncertain_delete || plan->destination_location.kind==PSMTP_LOCATION_PHONE)) app.phone_dirty=1;
    fprintf(app.stream,"stage=clipboard-operation action=%d status=%d files=%zu removed=%zu cleanup-warning=%d names-in-log=0\n",action,rc,plan->copied,plan->removed,plan->cleanup_failed);
    free(plan);app.copying=0;copy_checkpoint();(void)psmtp_screen_buttons();
}
static void reload_local(void) {
    char path[256];if(!app.local_current || app.view!=PSMTP_VIEW_LOCAL) return;
    strcpy(path,app.local_current->path);local_result(psmtp_local_load(app.local_current,path),app.local_current);
}
static void file_action(size_t action) {
    psmtp_op_ref source;int rc;
    if(action==4) {app.clipboard_active=0;strcpy(app.status,"Clipboard cleared.");return;}
    if(action==0 || action==1) {
        rc=capture_selection(&source);if(rc) {snprintf(app.status,sizeof(app.status),"Could not select this item (%d).",rc);return;}
        app.clipboard=source;app.clipboard_active=1;app.clipboard_cut=action==1;
        strcpy(app.status,action==1?"Cut selected. Browse to a destination and choose Paste here.":"Copied selection to clipboard. Browse to a destination and choose Paste here.");return;
    }
    if(action==3) {
        rc=capture_selection(&source);if(rc) {strcpy(app.status,"Select a file or folder first.");return;}
        run_operation(&source,app.phone_current?app.phone_current->client:NULL,2,NULL,NULL);
        if(app.phone_current && !app.phone_current->client->poisoned) {app.phone_dirty=0;(void)mtp_nav_refresh(app.phone_current);}else reload_local();return;
    }
    if(action!=2) return;
    if(!app.clipboard_active) {strcpy(app.status,"The clipboard is empty. Copy or cut an item first.");return;}
    if(app.phone_current){
        if(app.clipboard.location.kind==PSMTP_LOCATION_PHONE){strcpy(app.status,"Choose a file from Internal storage or USB to paste into the phone.");return;}
        run_operation(&app.clipboard,app.phone_current->client,app.clipboard_cut?1:0,&app.target,NULL);
        if(!app.phone_current->client->poisoned){app.phone_dirty=0;(void)mtp_nav_refresh(app.phone_current);}return;
    }
    if(app.view!=PSMTP_VIEW_LOCAL){strcpy(app.status,"Open a destination folder first.");return;}
    if(!app.local_current || app.local_current->error) {strcpy(app.status,"Open a readable destination folder first.");return;}
    if(app.clipboard.location.kind==PSMTP_LOCATION_PHONE && app.retained_phone && !app.retained_phone->client->poisoned && psmtp_location_same(&app.clipboard.location,&app.retained_location)) {
        run_operation(&app.clipboard,app.retained_phone->client,app.clipboard_cut?1:0,&app.target,app.local_current->path);reload_local();
    } else if(app.clipboard.location.kind==PSMTP_LOCATION_PHONE) {
        int fd=psmtp_native_directory(app.target.root,app.local_current->path);if(fd<0) {strcpy(app.status,"Destination is unavailable.");return;}
        rc=psmtp_native_status(fd,&app.paste_stat);if(close(fd)) rc=MTP_FILESYSTEM;if(rc) {strcpy(app.status,"Could not check the destination.");return;}
        app.paste_location=app.target;strcpy(app.paste_path,app.local_current->path);app.paste_pending=1;
        app.target=app.clipboard.location;app.switch_requested=1;
    } else {run_operation(&app.clipboard,NULL,app.clipboard_cut?1:0,&app.target,app.local_current->path);reload_local();}
}
static int browse(void *unused,mtp_client *client,FILE *out) {
    mtp_navigator *nav=malloc(sizeof(*nav));int rc=0,closed;uint32_t buttons;psmtp_location session_location=app.target;(void)unused;
    if(!nav) return MTP_MEMORY;
    app.view=PSMTP_VIEW_PHONE;
    rc=mtp_nav_open_ex(nav,client,out,app.paste_pending?NULL:catalog_progress,NULL);if(rc) goto done;
    if(app.sort_mode)(void)mtp_nav_sort(nav,app.sort_mode);app.toolbar_focus=0;
    app.phone_current=app.retained_phone=nav;app.retained_location=session_location;app.phone_detached=app.phone_dirty=0;
    if(app.paste_pending) {
        run_operation(&app.clipboard,client,app.clipboard_cut?1:0,&app.paste_location,app.paste_path);
        app.target=app.paste_location;strcpy(app.resume_path,app.paste_path);app.paste_pending=0;app.switch_requested=1;goto done;
    }
    if(app.locations.count) {snprintf(app.locations.entries[app.locations.active].label,sizeof(app.locations.entries[0].label),"%.95s",nav->device_name);app.locations.entries[app.locations.active].named=2;}
    strcpy(app.status,"X opens folders or reviews files. Options has Copy and Delete. Left switches devices.");copy_checkpoint();
    (void)psmtp_screen_buttons();
    for(;;) {
        const mtp_nav_entry *entry;int handled;
        (void)poll_devices(0);
        if(app.phone_detached || (!psmtp_preview_phone_busy() && client->poisoned)) {rc=MTP_DISCONNECTED;break;}
        if(app.refresh_requested) {app.refresh_requested=0;(void)poll_devices(1);}
        if(app.target.kind!=PSMTP_LOCATION_PHONE) {
            psmtp_console_selection panel={0};app.phone_current=NULL;
            if(app.switch_requested) {app.switch_requested=0;activate_local(app.local_current);}
            idle_refresh();panel.view=PSMTP_VIEW_LOCAL;panel.local=app.local_current;
            draw_ui(&app.ring,0,app.status,1,app.scan_status,&panel,NULL);
            if(psmtp_screen_error() || !psmtp_screen_pad_ready()) {rc=MTP_IO;break;}
            buttons=file_buttons();if(!shared_controls(buttons)) local_step(app.local_current,buttons);
            if(usb_target_missing()) internal_fallback(app.local_current,"USB drive disconnected. Switched to internal storage.");
            if(app.exit_requested) break;
            if(app.switch_requested && app.target.kind==PSMTP_LOCATION_PHONE && !psmtp_location_same(&app.target,&session_location)) break;
            sceKernelUsleep(1000);continue;
        }
        if(!psmtp_location_same(&app.target,&session_location)) break;
        if(app.switch_requested) {
            size_t i;int refreshed=app.phone_dirty;app.switch_requested=0;app.view=PSMTP_VIEW_PHONE;app.phone_current=nav;
            if(app.phone_dirty) {app.phone_dirty=0;rc=mtp_nav_refresh(nav);if(rc) break;}
            for(i=0;i<app.locations.count;++i) if(psmtp_location_same(&session_location,&app.locations.entries[i])) {app.locations.active=app.locations.selected=i;app.target=app.locations.entries[i];break;}
            strcpy(app.status,"Phone folder preserved. X opens; Triangle opens Options.");
            fprintf(out,"stage=phone-resume cached-session=1 metadata-refreshed=%d\n",refreshed);
        }
        idle_refresh();
        draw_ui(&app.ring,0,app.status,1,app.scan_status,NULL,nav);
        if(psmtp_screen_error() || !psmtp_screen_pad_ready()) {rc=MTP_IO;break;}
        buttons=file_buttons();handled=shared_controls(buttons);
        if(app.exit_requested) break;
        if(app.refresh_requested || app.switch_requested) continue;
        if(handled) {sceKernelUsleep(1000);continue;}
        if(!psmtp_preview_phone_busy() && client->poisoned) {rc=MTP_IO;break;}
        entry=mtp_nav_selected(nav);
        if((buttons&ORBIS_PAD_BUTTON_CROSS) && (!entry || !mtp_is_folder(&entry->info))) {
            if(entry) {
                if(psmtp_pkg_name(entry->info.name)){rc=install_selection();if(client->poisoned)break;rc=0;continue;}
                properties();rc=0;
                if(client->poisoned || rc==MTP_DISCONNECTED || rc==MTP_TIMEOUT || rc==MTP_IO) break;
                rc=0;
            } else strcpy(app.status,"This folder is empty. Circle goes back.");
        } else if((buttons&ORBIS_PAD_BUTTON_CROSS) && nav->depth==MTP_NAV_DEPTH) strcpy(app.status,"Folder depth limit reached. Circle goes back.");
        else if((buttons&ORBIS_PAD_BUTTON_CIRCLE) && !nav->depth) {app.drawer=1;app.locations.selected=app.locations.active;}
        else if(buttons&(ORBIS_PAD_BUTTON_LEFT|ORBIS_PAD_BUTTON_RIGHT|ORBIS_PAD_BUTTON_UP|ORBIS_PAD_BUTTON_DOWN|ORBIS_PAD_BUTTON_L1|ORBIS_PAD_BUTTON_R1|ORBIS_PAD_BUTTON_CROSS|ORBIS_PAD_BUTTON_CIRCLE|ORBIS_PAD_BUTTON_L2|ORBIS_PAD_BUTTON_R2|ORBIS_PAD_BUTTON_TRIANGLE)) {
            if(buttons&ORBIS_PAD_BUTTON_CIRCLE) rc=mtp_nav_back(nav);
            else if(buttons&ORBIS_PAD_BUTTON_CROSS) rc=mtp_nav_enter(nav);
            else if(buttons&ORBIS_PAD_BUTTON_UP) rc=mtp_nav_move(nav,app.list_view?-1:-4);
            else if(buttons&ORBIS_PAD_BUTTON_DOWN) rc=mtp_nav_move(nav,app.list_view?1:4);
            else if(buttons&ORBIS_PAD_BUTTON_LEFT) rc=mtp_nav_move(nav,-1);
            else if(buttons&ORBIS_PAD_BUTTON_RIGHT) rc=mtp_nav_move(nav,1);
            else if(buttons&ORBIS_PAD_BUTTON_L1) rc=mtp_nav_move(nav,app.list_view?-16:-12);
            else if(buttons&ORBIS_PAD_BUTTON_R1) rc=mtp_nav_move(nav,app.list_view?16:12);
            else if(buttons&ORBIS_PAD_BUTTON_L2) rc=nav->storage_index?mtp_nav_storage(nav,nav->storage_index-1):0;
            else if(buttons&ORBIS_PAD_BUTTON_R2) rc=nav->storage_index+1<nav->storage_count?mtp_nav_storage(nav,nav->storage_index+1):0;
            else rc=mtp_nav_refresh(nav);
            if(rc) break;
            strcpy(app.status,"X opens folders or reviews files. Options has Copy and Delete. Left switches devices.");copy_checkpoint();
        }
        sceKernelUsleep(1000);
    }
done:
    fprintf(out,"stage=browser-stop status=%d (%s)\n",rc,mtp_error_string(rc));
    if(app.view==PSMTP_VIEW_LOCAL && !app.exit_requested && !app.paste_pending && !app.switch_requested) {
        strcpy(app.resume_path,app.local_current->path);app.switch_requested=1;
    } else if(app.phone_detached && app.target.kind==PSMTP_LOCATION_PHONE) {
        size_t i;for(i=0;i<app.locations.count;++i) if(app.locations.entries[i].kind==PSMTP_LOCATION_INTERNAL) {app.target=app.locations.entries[i];app.switch_requested=1;break;}
        strcpy(app.status,"Phone disconnected. Switched to internal storage.");
    }
    preview_quiet();app.phone_current=app.retained_phone=NULL;app.phone_detached=app.phone_dirty=0;closed=mtp_nav_close(nav);free(nav);return rc?rc:closed;
}
static void activate_local(psmtp_local_browser *local) {
    size_t i;int rc;
    for(i=0;i<app.locations.count;++i) if(psmtp_location_same(&app.target,&app.locations.entries[i])) break;
    if(i==app.locations.count) {
        for(i=0;i<app.locations.count;++i) if(app.locations.entries[i].kind==PSMTP_LOCATION_INTERNAL) {app.target=app.locations.entries[i];break;}
        if(i==app.locations.count) return;
    }
    app.locations.active=app.locations.selected=i;app.view=PSMTP_VIEW_LOCAL;
    rc=psmtp_local_browse(local,app.target.root,app.resume_path[0]?app.resume_path:app.target.kind==PSMTP_LOCATION_INTERNAL?"/data":app.target.root);
    app.resume_path[0]=0;psmtp_local_sort(local,app.sort_mode);app.toolbar_focus=0;
    local_result(rc,local);
    if(app.paste_path[0] && !app.paste_pending) {app.paste_path[0]=0;return;}
    snprintf(app.status,sizeof(app.status),rc?"Could not open device (error %d). Reconnect and retry.":"X opens folders and file details. Left switches devices.",local->error);
}
static void local_step(psmtp_local_browser *local,uint32_t buttons) {
    int rc;size_t stride=app.list_view?1:4,page=app.list_view?16:12;size_t step=buttons&ORBIS_PAD_BUTTON_L1?page:buttons&ORBIS_PAD_BUTTON_LEFT?1:stride;
    if(buttons&(ORBIS_PAD_BUTTON_UP|ORBIS_PAD_BUTTON_L1|ORBIS_PAD_BUTTON_LEFT)) local->selected=step>local->selected?0:local->selected-step;
    step=buttons&ORBIS_PAD_BUTTON_R1?page:buttons&ORBIS_PAD_BUTTON_RIGHT?1:stride;
    if((buttons&(ORBIS_PAD_BUTTON_DOWN|ORBIS_PAD_BUTTON_R1|ORBIS_PAD_BUTTON_RIGHT)) && local->count) local->selected=step>local->count-1-local->selected?local->count-1:local->selected+step;
    if(buttons&ORBIS_PAD_BUTTON_TRIANGLE) {char path[PSMTP_LOCAL_PATH];strcpy(path,local->path);rc=psmtp_local_load(local,path);local_result(rc,local);strcpy(app.status,rc?"Refresh failed. Check the device and retry.":"Folder refreshed.");}
    if(buttons&ORBIS_PAD_BUTTON_CIRCLE) {
        if(!strcmp(local->path,local->root)) {app.drawer=1;app.locations.selected=app.locations.active;}
        else {rc=psmtp_local_back(local);local_result(rc,local);if(rc) strcpy(app.status,"Could not open parent folder. Check storage access.");}
    }
    if((buttons&ORBIS_PAD_BUTTON_CROSS) && local->count) {
        if(local->entries[local->selected].folder) {
            rc=psmtp_local_enter(local);local_result(rc,local);
            if(rc) snprintf(app.status,sizeof(app.status),"Folder error %d at %s. Touchpad retries; Circle returns.",local->error,local->error_stage?local->error_stage:"unknown");
            else strcpy(app.status,"Left switches devices. Circle goes back.");
        }
        else if(psmtp_pkg_name(local->entries[local->selected].name))(void)install_selection();
        else properties();
    }
}
static void internal_fallback(psmtp_local_browser *local,const char *message) {
    size_t i;for(i=0;i<app.locations.count;++i) if(app.locations.entries[i].kind==PSMTP_LOCATION_INTERNAL) {
        app.target=app.locations.entries[i];activate_local(local);break;
    }
    if(message) snprintf(app.status,sizeof(app.status),"%s",message);
}
#ifndef PSMTP_TEST_EXPLORER
int main(void) {
    uint32_t buttons;int rc,access_rc;
#ifndef PSMTP_PRODUCTION
    cookie_io_functions_t sink={0};int log_errno;
#endif
    psmtp_local_browser *local=calloc(1,sizeof(*local));
    diag_log_init(&app.ring);app.names=1;app.initial_launch=1;app.refresh_requested=1;
    app.view=PSMTP_VIEW_HOME;strcpy(app.status,"Finding connected devices...");strcpy(app.scan_status,"Logs ready");
#ifndef PSMTP_PRODUCTION
    app.file=open_log(PSMTP_DIAGNOSTIC_LOG_PATH,"a");log_errno=errno;if(app.file) setvbuf(app.file,NULL,_IONBF,0);
    sink.write=log_write;app.stream=fopencookie(NULL,"w",sink);if(!app.stream) psmtp_ps4_exit(1,app.file);setvbuf(app.stream,NULL,_IONBF,0);
    fputs("\n=== PS MTP Explorer " PSMTP_VERSION " ===\n",app.stream);
    if(!app.file) fprintf(app.stream,"General log unavailable errno=%d\n",log_errno);
#else
    app.stream=psmtp_release_log_stream();
#endif


    scan_log_begin();fputs("=== BEGIN DEVICE EXPLORER " PSMTP_VERSION " names=0 ===\n",app.stream);
    fprintf(app.stream,"stage=process-access uid=%u euid=%u in-sandbox=%d\n",(unsigned)getuid(),(unsigned)geteuid(),sceKernelIsInSandbox());
    access_rc=psmtp_process_access(app.stream);
    if(!access_rc) {uint64_t available,block;int space_rc=psmtp_native_space("/","/data",&available,&block,app.stream);
        fprintf(app.stream,"stage=storage-startup status=%d available=%" PRIu64 "\n",space_rc,available);}
    copy_checkpoint();
    psmtp_preview_open(app.stream);
    rc=psmtp_screen_open(app.stream);
    if(rc || !local) {psmtp_preview_close(app.stream);scan_log_end();psmtp_screen_close(app.stream);if(app.stream)fclose(app.stream);psmtp_ps4_exit(1,app.file);}
    app.local_current=local;local->scan_progress=catalog_progress;local->scan_user=local;
    app.screen_ready=1;
    if(access_rc!=0) {
#ifdef PSMTP_PRODUCTION
        install_notice("Storage unavailable","Storage access could not be initialized. Close the app, enable GoldHEN and try again.");
#else
        diagnostics();
#endif
        goto shutdown;
    }
    psmtp_audio_open(app.stream);
    rc=psmtp_local_prepare_copies(app.stream);
    if(rc) fprintf(app.stream,"stage=storage-ready status=%d errno=%d; copies unavailable, no permission changes attempted\n",rc,errno);
    copy_checkpoint();
    app.usb_mask=psmtp_local_usb_scan(app.stream);psmtp_locations_build(&app.locations,NULL,0,app.usb_mask);
    while(!app.exit_requested) {
        psmtp_console_selection panel={0};
        if(app.refresh_requested || (app.switch_requested && app.target.kind==PSMTP_LOCATION_PHONE)) {
            psmtp_ps4_options options={0};int connecting=app.initial_launch || (app.switch_requested && app.target.kind==PSMTP_LOCATION_PHONE);
            app.refresh_requested=app.switch_requested=0;
            options.communicate=connecting;options.names=1;options.choose_device=choose;options.devices=devices;options.browse=browse;
            panel.view=PSMTP_VIEW_HOME;strcpy(app.status,connecting?"Opening the connected phone...":"Refreshing connected devices...");
            if(app.initial_launch) draw_ui(&app.ring,0,app.status,1,app.scan_status,&panel,NULL);
            if(!psmtp_screen_pad_ready()) {app.exit_requested=1;break;}
            rc=psmtp_ps4_diagnose(&options,app.stream);
            if(app.paste_pending) {app.paste_pending=0;app.target=app.paste_location;strcpy(app.resume_path,app.paste_path);app.switch_requested=1;strcpy(app.status,"Source phone unavailable. Nothing was pasted or removed.");}
            fprintf(app.stream,"stage=device-cycle status=%d\n",rc);copy_checkpoint();app.initial_launch=0;
            if(app.exit_requested) break;
            if(!app.switch_requested && !app.refresh_requested) {
                if(!connecting && app.target.kind!=PSMTP_LOCATION_PHONE && app.target.root[0]) activate_local(local);
                else internal_fallback(local,connecting?"Phone unavailable. Unlock it, select File Transfer and reopen the device.":NULL);
            }
            continue;
        }
        if(app.switch_requested) {app.switch_requested=0;activate_local(local);}
        idle_refresh();panel.view=PSMTP_VIEW_LOCAL;panel.local=local;
        draw_ui(&app.ring,0,app.status,1,app.scan_status,&panel,NULL);
        if(psmtp_screen_error()) break;
        buttons=file_buttons();if(!shared_controls(buttons)) local_step(local,buttons);
        if(poll_devices(0)) internal_fallback(local,"USB drive unavailable. Switched to internal storage.");
        sceKernelUsleep(1000);
    }
shutdown:
    psmtp_install_shutdown(app.stream);
    psmtp_preview_close(app.stream);
    psmtp_audio_close(app.stream);
    psmtp_ps4_usb_shutdown(app.stream);
    free(local);fputs("=== END DEVICE EXPLORER ===\n",app.stream);scan_log_end();
    app.screen_ready=0;psmtp_screen_close(app.stream);if(app.stream)fclose(app.stream);app.stream=NULL;
    psmtp_ps4_exit(psmtp_screen_error() || access_rc!=0?1:0,app.file);
}
#endif
