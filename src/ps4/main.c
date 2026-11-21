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
#include "lifecycle.h"
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
#include <strings.h>
#include <unistd.h>
#include <time.h>
#include "monotonic.h"
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
#include "work.h"
#include "download.h"
#include "update_client.h"
#include "system_dialog.h"
#include "preferences.h"
#include "notify.h"
#include "ime.h"
#include "release_config.h"
#include "usb_download.h"
#ifndef PSMTP_TEST_EXPLORER
#include "lan_server.h"
#include "viewer.h"
#include "media_player.h"
#include "archive_job.h"
#endif
#include "psmtp/direct_download.h"

#include "psmtp/cleanup_log.h"

#define MULTI_LIMIT 128u
#define PROGRESS_UPDATE_MS 1000u

static struct {
    psmtp_shutdown shutdown;
    diag_log ring;
    FILE *file,*stream,*scan_file;
    psmtp_work *work;int install_collecting;psmtp_update_job *update_job;
#ifndef PSMTP_TEST_EXPLORER
    psmtp_media_player *media_player;
    int media_retiring;
    struct {
        int pending;
        uint32_t storage;
        size_t depth;
        uint8_t identity[32];
        char names[MTP_NAV_DEPTH+1][MTP_STRING_BYTES];
    } media_return;
#endif
    psmtp_work *usb_receiver;
    psmtp_usb_download_report usb_receiver_report;
    uint64_t usb_receiver_retry;
    psmtp_transfer_meter usb_receive_meter;
    uint64_t usb_source_speed,usb_speed_time;
    int usb_receiver_running,usb_progress_hidden,usb_install_dispatched,lan_revoked,download_mode;
    uint64_t busy_epoch;
    size_t scroll;
    int screen_ready,names,pad_failure_reported,drawing;
    int scan_started,scan_failed;
    int copying,view,shutting_down;
    psmtp_locations locations;
    psmtp_ps4_candidate phones[128];size_t phone_count;
    unsigned usb_mask;
    int drawer,exit_requested,refresh_requested,switch_requested,initial_launch;
    int list_view,drawer_settings,open_requested,progress_hidden,install_mode,direct_download;
    int toolbar_focus,sort_mode,preview_pause,settings_open,settings_index,theme;
    psmtp_preferences preferences;int preferences_ready,preferences_unsaved;
    psmtp_location target,paste_location;
    psmtp_op_ref clipboard;int clipboard_active,clipboard_cut,paste_pending;
    psmtp_op_ref marked[MULTI_LIMIT],clipboard_batch[MULTI_LIMIT];
    size_t marked_count,batch_count;
    int multi_mode;
    psmtp_location multi_location;
    uint32_t multi_parent,multi_storage;
    char multi_path[PSMTP_LOCAL_PATH];
    char paste_path[256],resume_path[256],clipboard_label[320];psmtp_native_stat paste_stat;
    psmtp_local_browser *local_current;mtp_navigator *phone_current,*retained_phone;
    psmtp_location retained_location;int monitor_live,poll_failed,phone_detached,phone_dirty;
    char last_name[MTP_STRING_BYTES],last_result[128];
    char copy_status[128];
    uint64_t last_draw,mount_probe,loading_started,folder_poll,folder_metadata_poll,install_poll;
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
static void archive_selection(int);
static int capture_selection(psmtp_op_ref *);
static int multi_marked(size_t);
static void multi_check_scope(void);
static void multi_toggle(void);
static void batch_action(size_t);
static void multi_end(void){app.multi_mode=0;app.marked_count=0;}
static void notice_dialog(const char *,const char *);
static void usb_receiver_tick(uint64_t);
static int accessory_owns_usb(void){
    /* Keep USB exclusive until an accessory worker has been joined. */
    return app.direct_download==1 ||
        (app.usb_receiver_running && !app.usb_receiver_report.wireless);
}
static void receiver_mode(int mode){
    if(app.direct_download==2 && mode!=2){
        if(app.usb_receiver_running && app.usb_receiver_report.wireless)app.lan_revoked=1;
#ifndef PSMTP_TEST_EXPLORER
        else psmtp_lan_forget();
#endif
    }
    if(mode)app.download_mode=mode;
    app.direct_download=mode;app.usb_receiver_retry=0;
}
static int usb_transfer_snapshot(psmtp_work_progress_state *progress){
    return app.usb_receiver_running && psmtp_work_snapshot(app.usb_receiver,progress) &&
        (progress->phase==MTP_COPY_RECEIVE || progress->phase==MTP_COPY_PUBLISH);
}
static int usb_progress_pending(void){
    psmtp_work_progress_state progress;
    return app.direct_download && (app.direct_download==2)==app.usb_receiver_report.wireless &&
        !app.usb_install_dispatched && !app.copying &&
        (!app.usb_progress_hidden || __atomic_load_n(&app.usb_receiver_report.install,__ATOMIC_ACQUIRE)) &&
        usb_transfer_snapshot(&progress);
}
static void load_preferences(void){
    psmtp_preferences p;int rc=psmtp_preferences_load(&p,app.stream);
    app.preferences=p;app.preferences_ready=1;app.preferences_unsaved=0;
    app.list_view=p.list_view;app.sort_mode=p.sort_mode;psmtp_stream_boost_set(p.speed_boost);app.install_mode=p.install_mode;app.theme=p.theme;app.direct_download=0;
    if(rc)fputs("Preferences unavailable; using defaults. Change an option to save new choices.\n",app.stream);
}
static void save_preferences(void){
    psmtp_preferences p={app.list_view,app.sort_mode,psmtp_stream_boost_get(),app.install_mode,app.theme};
    if(!app.preferences_ready || app.copying || app.shutting_down)return;
    if(!app.preferences_unsaved && p.list_view==app.preferences.list_view && p.sort_mode==app.preferences.sort_mode && p.speed_boost==app.preferences.speed_boost && p.install_mode==app.preferences.install_mode && p.theme==app.preferences.theme)return;
    if(psmtp_preferences_save(&p,app.stream)){
        app.preferences_unsaved=1;snprintf(app.status,sizeof(app.status),"Choice applied, but preferences could not be saved. Check storage access.");
    }else {app.preferences=p;app.preferences_unsaved=0;}
}
static void restore_defaults(void){
    psmtp_preferences defaults;int rc=0;
    if(app.copying || app.shutting_down)return;
    psmtp_preferences_defaults(&defaults);app.list_view=defaults.list_view;app.sort_mode=defaults.sort_mode;
    psmtp_stream_boost_set(defaults.speed_boost);app.install_mode=defaults.install_mode;app.theme=defaults.theme;receiver_mode(0);app.toolbar_focus=0;
    if(app.phone_current && !app.phone_detached && !app.phone_current->client->poisoned)rc=mtp_nav_sort(app.phone_current,defaults.sort_mode);
    else if(app.local_current)psmtp_local_sort(app.local_current,defaults.sort_mode);
    /* Force a durable default record even if the current values were defaults
     * or the previous file was externally edited/unreadable. */
    app.preferences_unsaved=1;save_preferences();
    if(!app.preferences_unsaved)snprintf(app.status,sizeof(app.status),"%s",rc?"Defaults saved. Refresh the folder to update its order.":"Default settings restored.");
    fprintf(app.stream,"stage=preferences-restore-defaults saved=%d sort-status=%d\n",!app.preferences_unsaved,rc);
    notice_dialog(app.preferences_unsaved?"Defaults could not be saved":"Defaults restored",
        app.preferences_unsaved?"Default settings are active for this session.\nCheck storage access and try again.":
        rc?"Default settings have been saved.\nRefresh the folder to update its order.":
        "Your app settings have been restored.\nThe changes have been saved.");
}
static void transfer_notice(int action,int status,int finished){
    const char *message;
    if(action!=0 && action!=1)return; /* Deletion and clipboard selection are silent. */
    if(!finished)message=action?"Move started. Keep source and destination connected.":"Copy started. Keep source and destination connected.";
    else if(status==MTP_CANCELLED)message=action?"Move cancelled.":"Copy cancelled.";
    else if(status)message=action?"Move stopped. Check the app for details.":"Copy stopped. Check the app for details.";
    else message=action?"Move complete.":"Copy complete.";
    (void)psmtp_notify(message,app.stream);
}
static void usb_receive_meter_update(int phase,uint64_t done,uint64_t total,uint64_t now){
    uint64_t clamped=done>total?total:done;
    if(!now)return;
    if(!app.usb_receive_meter.active || app.usb_receive_meter.phase!=phase ||
       app.usb_receive_meter.total!=total || clamped<app.usb_receive_meter.last_done ||
       now<app.usb_receive_meter.last_time || now-app.usb_receive_meter.last_time>=PROGRESS_UPDATE_MS)
        psmtp_transfer_measure(&app.usb_receive_meter,phase,done,total,now);
}
static void draw_ui(const diag_log *log,size_t offset,const char *status,int names,const char *save,const psmtp_console_selection *panel,const mtp_navigator *nav) {
    psmtp_console_selection visible={0};
    /* Rendering and preview completion can write to the live diagnostic sink.
     * Keep those lines, but never redraw from inside the current frame. */
    if(app.drawing || app.shutting_down || psmtp_ps4_exit_pending()) return;
    if(psmtp_lifecycle_background()){psmtp_screen_submit_done();sceKernelUsleep(16000);return;}
    app.drawing=1;
    struct timespec ts={0};
    uint64_t now=psmtp_monotonic_time(&ts)?0:(uint64_t)ts.tv_sec*1000+(uint64_t)ts.tv_nsec/1000000;
    if(now && (app.direct_download || app.usb_receiver_running))usb_receiver_tick(now);
    if(panel) visible=*panel;else visible.view=app.view;
    visible.settings_context=app.settings_open;visible.settings_index=app.settings_index;
    visible.theme=app.theme;
    visible.locations=&app.locations;visible.drawer_focus=app.drawer;
    visible.preferences_unsaved=app.preferences_unsaved;visible.speed_boost=psmtp_stream_boost_get();visible.install_mode=app.install_mode;visible.direct_download=app.direct_download;visible.list_view=app.list_view;visible.drawer_settings=app.drawer_settings;
    if(app.direct_download && app.usb_receiver_running){
        psmtp_work_progress_state progress={0};
        psmtp_lan_pair_ui *pairing=&app.usb_receiver_report.pairing;
        __atomic_store_n(&pairing->visible,app.direct_download==2 && app.usb_receiver_report.wireless &&
            visible.view==PSMTP_VIEW_SETTINGS && visible.index==PSMTP_SETTINGS_DOWNLOAD_MODE,__ATOMIC_RELEASE);
        visible.usb_receiver_ready=__atomic_load_n(&app.usb_receiver_report.ready,__ATOMIC_ACQUIRE);
        visible.wireless=app.usb_receiver_report.wireless;
        visible.usb_part_count=__atomic_load_n(&app.usb_receiver_report.part_count,__ATOMIC_ACQUIRE);
        visible.usb_part_percentages=__atomic_load_n(&app.usb_receiver_report.part_percentages,__ATOMIC_RELAXED);
        if(visible.usb_part_count>1){
            uint64_t speed=__atomic_load_n(&app.usb_receiver_report.source_speed,__ATOMIC_RELAXED);
            if(!now || !app.usb_speed_time || now<app.usb_speed_time || now-app.usb_speed_time>=PROGRESS_UPDATE_MS){
                app.usb_source_speed=speed;
                app.usb_speed_time=now;
            }
            visible.usb_source_speed=app.usb_source_speed;
        }else {app.usb_source_speed=0;app.usb_speed_time=0;}
        if(app.usb_receiver_report.wireless && visible.usb_receiver_ready && app.direct_download==2){
            visible.lan_address=app.usb_receiver_report.lan_address;
            visible.lan_qr=app.usb_receiver_report.lan_qr;
            visible.lan_code=__atomic_load_n(&pairing->code,__ATOMIC_ACQUIRE);
            visible.lan_decision=__atomic_load_n(&pairing->decision,__ATOMIC_ACQUIRE);
        }
        if((app.usb_progress_hidden || visible.usb_install) && usb_transfer_snapshot(&progress)){
            visible.usb_receive_phase=progress.phase;
            visible.usb_receive_done=progress.done;
            visible.usb_receive_total=progress.total;
            usb_receive_meter_update(progress.phase,progress.done,progress.total,now);
            visible.usb_receive_meter=app.usb_receive_meter;
        }
    }
    visible.browser=app.phone_current;
    if(!visible.local && app.view==PSMTP_VIEW_LOCAL) visible.local=app.local_current;
    if(app.clipboard_active) {
        if(app.batch_count)snprintf(app.clipboard_label,sizeof(app.clipboard_label),"%s: %zu items",app.clipboard_cut?"Cut":"Copy",app.batch_count);
        else snprintf(app.clipboard_label,sizeof(app.clipboard_label),"%s: %.255s",app.clipboard_cut?"Cut":"Copy",app.clipboard.name);
        visible.clipboard_label=app.clipboard_label;
    }
    multi_check_scope();
    visible.multi_mode=app.multi_mode;visible.marked_count=app.marked_count;visible.marked=app.multi_mode?multi_marked:NULL;
    visible.can_select=(app.phone_current && mtp_nav_selected(app.phone_current)) || (app.view==PSMTP_VIEW_LOCAL && app.local_current && app.local_current->count);
#ifndef PSMTP_TEST_EXPLORER
    if(app.phone_current){
        const mtp_nav_entry *entry=mtp_nav_selected(app.phone_current);
        visible.can_extract=entry && !mtp_is_folder(&entry->info) && psmtp_archive_suffix(entry->info.name)!=NULL;
    }else if(visible.can_select){
        const psmtp_local_entry *entry=&app.local_current->entries[app.local_current->selected];
        visible.can_extract=!entry->folder && !entry->incomplete && psmtp_archive_suffix(entry->name)!=NULL;
    }
#endif
    visible.can_paste=paste_available();
    visible.can_create=app.phone_current || (app.view==PSMTP_VIEW_LOCAL && app.local_current && !app.local_current->error);
    visible.toolbar_focus=app.toolbar_focus;visible.sort_mode=app.sort_mode;visible.rename_allowed=visible.can_select && rename_available();
    psmtp_preview_frame(&app.target,app.phone_current,app.view==PSMTP_VIEW_LOCAL?app.local_current:NULL,app.list_view,!app.preview_pause && !app.copying && (visible.view==PSMTP_VIEW_PHONE || visible.view==PSMTP_VIEW_LOCAL || visible.view==PSMTP_VIEW_INFO),now);
    visible.metadata=psmtp_preview_lookup;
    visible.metadata_revision=psmtp_preview_revision();visible.metadata_revision_known=1;
    psmtp_screen_draw(log,offset,status,names,save,&visible,nav);
    app.drawing=0;
}

#ifndef PSMTP_PRODUCTION
static FILE *open_log(const char *path,const char *mode) {
    if(mkdir(PSMTP_LOG_DIRECTORY,0755) && errno!=EEXIST) return NULL;
    /* Existing directories are reused. If the path is an existing file or
     * inaccessible, fopen reports the error; never fall back to /data files. */
    return fopen(path,mode);
}

/* This sink sees completed screen lines only while a scan is active.
 * The existing full diagnostic log remains available for launch/exit faults. */
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
    /* Append across scans and launches so a failed connection is preserved. */
    app.scan_file=open_log(PSMTP_SCAN_LOG_PATH,"a");
    saved_errno=errno;
    if(!app.scan_file) {
        app.scan_failed=1;
        snprintf(app.scan_status,sizeof(app.scan_status),"UNAVAILABLE (errno=%d)",saved_errno);
        fprintf(app.stream,"Scan log unavailable errno=%d; general log still attempted.\n",saved_errno);
        return;
    }
    app.scan_started=1;
    if(setvbuf(app.scan_file,NULL,_IOFBF,65536)) {
        app.scan_failed=1;
        strcpy(app.scan_status,"UNAVAILABLE - cannot enable live saving");
        fclose(app.scan_file); app.scan_file=NULL;
        return;
    }
    strcpy(app.scan_status,"SAVING");
    diag_log_set_sink(&app.ring,scan_line,NULL);
}
static ssize_t log_write(void *unused,const char *p,size_t n) {
    (void)unused;if(app.shutting_down)return (ssize_t)n;
    diag_log_write(&app.ring,p,n);
    if(app.file && fwrite(p,1,n,app.file)!=n) {
        const char message[]="\nLOG FILE WRITE FAILED; photograph TV results.\n";
        fclose(app.file); app.file=NULL;
        diag_log_write(&app.ring,message,sizeof(message)-1);
    }
    if(app.screen_ready && !app.drawing && !app.copying && app.view==PSMTP_VIEW_LOGS && memchr(p,'\n',n)) {
        struct timespec ts={0}; uint64_t now;
        const char *line=app.ring.current?diag_log_line(&app.ring,app.ring.current-1):NULL;
        now=psmtp_monotonic_time(&ts)?app.last_draw+200:
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
    if(psmtp_preview_quiesce())psmtp_ps4_exit(1,app.file);
    app.preview_pause=0;
}
static uint32_t read_buttons(void) {
    if(psmtp_ps4_exit_pending()){
        app.exit_requested=1;return ORBIS_PAD_BUTTON_CIRCLE|ORBIS_PAD_BUTTON_OPTIONS;
    }
    if(psmtp_lifecycle_background())return 0;
    uint32_t buttons=psmtp_screen_buttons();
    if(buttons)preview_quiet();
    if(psmtp_screen_pad_error() && !app.pad_failure_reported) {
        app.pad_failure_reported=1;
        fprintf(app.stream,"stage=pad-read rc=0x%08" PRIx32 "; controller unavailable; reconnect input, transfers continue.\n",(uint32_t)psmtp_screen_pad_error());
    }
    if(!psmtp_screen_pad_error())app.pad_failure_reported=0;
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

#define TRANSFER_FRAME_MS 16u
#define INSTALL_FRAME_MS 100u
typedef struct { psmtp_console_selection panel; uint64_t drawn,started,next_present,next_update,previous_frame,max_gap;unsigned frames,progress_updates;int timing_phase,live,cancelling; } copy_ui;
static void draw_progress(copy_ui *ui,uint64_t now,const char *message) {
    psmtp_console_selection hidden={0};
    if(ui->live){if(ui->frames && now>=ui->previous_frame && now-ui->previous_frame>ui->max_gap)ui->max_gap=now-ui->previous_frame;
        ++ui->frames;ui->previous_frame=now;ui->next_present=now+(ui->panel.view==PSMTP_VIEW_INSTALL_PROGRESS?INSTALL_FRAME_MS:TRANSFER_FRAME_MS);ui->panel.animation_ms=now+1;}
    if(!ui->started || ui->timing_phase!=ui->panel.phase+1) {ui->started=now;ui->timing_phase=ui->panel.phase+1;}
    ui->panel.elapsed_ms=now-ui->started;
    if(!ui->live)psmtp_transfer_measure(&ui->panel.transfer,ui->panel.phase,ui->panel.done,ui->panel.total,now);
    if(app.progress_hidden && !ui->panel.usb_download) {hidden.view=app.view;hidden.transfer_active=1;hidden.busy_epoch=ui->panel.busy_epoch;draw_ui(&app.ring,0,"Transfer continues. X shows progress; Circle cancels.",1,app.scan_status,&hidden,app.phone_current);}
    else draw_ui(&app.ring,0,message,1,app.scan_status,&ui->panel,NULL);
}
static int progress_due(const copy_ui *ui,int phase,uint64_t done,uint64_t total,uint64_t now,uint32_t buttons,int cancel){
    if(buttons&ORBIS_PAD_BUTTON_CROSS)return 1;
    if(ui->live)return !ui->frames || now>=ui->next_present || ui->panel.phase!=phase || (cancel && !ui->cancelling);
    return ui->panel.phase!=phase || ((phase!=MTP_COPY_LOCAL && phase!=MTP_COPY_SOURCE && phase!=MTP_COPY_PUBLISH) && now-ui->drawn>=PROGRESS_UPDATE_MS) || done==total;
}
/* Numeric progress is sampled once per second. Cached frames and input have
 * independent deadlines, so slow USB/storage cannot impose a 1 FPS UI cap. */
static void progress_values(copy_ui *ui,int phase,uint64_t done,uint64_t total,uint64_t now,uint32_t buttons,int cancel){
    if(!ui->live || !ui->frames || now>=ui->next_update || ui->panel.phase!=phase || (buttons&ORBIS_PAD_BUTTON_CROSS) || (cancel && !ui->cancelling)){
        ui->panel.copy_mode=2;ui->panel.phase=phase;ui->panel.done=done;ui->panel.total=total;
        if(ui->live){ui->next_update=now+PROGRESS_UPDATE_MS;++ui->progress_updates;
            psmtp_transfer_measure(&ui->panel.transfer,phase,done,total,now);}
    }
}
static int copy_progress(void *v,enum mtp_copy_phase phase,uint64_t done,uint64_t total) {
    copy_ui *ui=(copy_ui *)v; uint32_t buttons; struct timespec ts={0}; uint64_t now;
    if(ui->live && psmtp_work_pause_requested(app.work))return 0;
    buttons=read_buttons();
    int cancel=psmtp_screen_error() || (buttons&(ORBIS_PAD_BUTTON_CIRCLE|ORBIS_PAD_BUTTON_OPTIONS));
    if(cancel && !ui->live)return 1;
    if((buttons&ORBIS_PAD_BUTTON_CROSS) && ui->panel.view!=PSMTP_VIEW_INSTALL_PROGRESS) app.progress_hidden=!app.progress_hidden;
    now=psmtp_monotonic_time(&ts)?ui->drawn+100:(uint64_t)ts.tv_sec*1000+(uint64_t)ts.tv_nsec/1000000;
    if(progress_due(ui,(int)phase,done,total,now,buttons,cancel)) {
        progress_values(ui,(int)phase,done,total,now,buttons,cancel);
        const char *message=phase==MTP_COPY_LOCAL?"Verifying...":phase==MTP_COPY_PREPARE?"Preparing operation; Circle cancels.":"Copying selected items; Circle cancels.";
        if(ui->panel.view==PSMTP_VIEW_INSTALL_PROGRESS)message="Keep PSMTP open while installing.";
        draw_progress(ui,now,ui->cancelling || cancel?"Cancelling; waiting for the current operation...":message);
        ui->drawn=now;
    }
    return cancel || psmtp_screen_error()!=0;
}
static uint64_t ui_now(void){struct timespec ts={0};if(psmtp_monotonic_time(&ts))return 0;return (uint64_t)ts.tv_sec*1000+(uint64_t)ts.tv_nsec/1000000;}
#if !defined(PSMTP_PRODUCTION) && !defined(PSMTP_TEST_EXPLORER)
static uint64_t clock_delta_ms(const struct timespec *before,const struct timespec *after){
    uint64_t a=(uint64_t)before->tv_sec*1000+(uint64_t)before->tv_nsec/1000000,b=(uint64_t)after->tv_sec*1000+(uint64_t)after->tv_nsec/1000000;
    return b>=a?b-a:0;
}
static void clock_diagnostic(void){
    struct timespec old_a={0},old_b={0},mono_a={0},mono_b={0},wall_a={0},wall_b={0};int status=0,sleep_status;
    if(psmtp_ps4_exit_pending())return;
    /* Deliberately query the old SDK clock only for this diagnostic. The
     * monotonic/transport/UI paths exclusively use native ID 4. */
    status|=clock_gettime(1,&old_a);status|=psmtp_monotonic_time(&mono_a);status|=clock_gettime(CLOCK_REALTIME,&wall_a);
    sleep_status=sceKernelUsleep(200000);
    status|=clock_gettime(1,&old_b);status|=psmtp_monotonic_time(&mono_b);status|=clock_gettime(CLOCK_REALTIME,&wall_b);
    fprintf(app.stream,"stage=clock-crosscheck native-monotonic-id=4 old-sdk-id=1 requested-sleep-ms=200 monotonic-ms=%" PRIu64 " realtime-ms=%" PRIu64 " old-clock-ms=%" PRIu64 " query-status=%d sleep-status=%d\n",clock_delta_ms(&mono_a,&mono_b),clock_delta_ms(&wall_a,&wall_b),clock_delta_ms(&old_a,&old_b),status,sleep_status);
}
#endif
static void worker_logs(psmtp_work *work){
    char bytes[8192];size_t n;unsigned dropped;if(!work)return;
    n=psmtp_work_read_log(work,bytes,sizeof(bytes));if(n)fwrite(bytes,1,n,app.stream);
    dropped=psmtp_work_dropped_logs(work);if(dropped)fprintf(app.stream,"\nstage=operation-log-overflow chunks=%u diagnostics-incomplete=1\n",dropped);
}
static void work_logs(void){worker_logs(app.work);}

#if !defined(PSMTP_TEST_EXPLORER) || defined(PSMTP_TEST_USB_RECEIVER)
static void usb_receiver_tick(uint64_t now){
    int rc;
    if(app.usb_receiver_running){
        if((!app.direct_download || (app.direct_download==2)!=app.usb_receiver_report.wireless ||
            (app.usb_receiver_report.wireless && app.lan_revoked)) &&
            !psmtp_work_cancelled(app.usb_receiver))psmtp_work_cancel(app.usb_receiver);
        worker_logs(app.usb_receiver);
        if(!psmtp_work_done(app.usb_receiver,&rc) || psmtp_work_join(app.usb_receiver,0))return;
        worker_logs(app.usb_receiver);
        app.usb_receiver_running=0;
        app.usb_receiver_retry=now+(app.usb_receiver_report.offered?250:2500);
        app.mount_probe=0;
        if(app.file)fflush(app.file);
        if(app.usb_receiver_report.offered && app.direct_download){
            if(app.usb_receiver_report.paused)strcpy(app.status,"Download paused. Resume from your phone.");
            else if(!rc){
                strcpy(app.status,app.usb_receiver_report.installing?"Direct Install completed.":"Download saved in " PSMTP_DOWNLOAD_DISPLAY);
                app.folder_poll=0;
            }else if(rc==MTP_EXISTS)strcpy(app.status,"Download stopped. That filename already exists.");
            else if(rc==MTP_NO_SPACE)strcpy(app.status,"Download stopped. PS4 storage is full.");
            else if(rc==MTP_CANCELLED)strcpy(app.status,"Download cancelled.");
            else if(rc==MTP_NO_INTERNET)strcpy(app.status,"No internet connection");
            else if(app.usb_receiver_report.source_error>=PSMTP_DOWNLOAD_HTTP_STATUS)
                snprintf(app.status,sizeof(app.status),"Download server returned HTTP %u.",
                    app.usb_receiver_report.source_error-PSMTP_DOWNLOAD_HTTP_STATUS);
            else if(app.usb_receiver_report.source_error)
                strcpy(app.status,"Could not open the PS4 download source. Check the link and try again.");
            else if(app.usb_receiver_report.file_stage)
                snprintf(app.status,sizeof(app.status),"Download stopped while %s (error %d).",
                    app.usb_receiver_report.file_stage,app.usb_receiver_report.file_error);
            else if(app.usb_receiver_report.installing)
                strcpy(app.status,"Direct Install stopped. Check the package and PS4 Notifications.");
            else if(app.usb_receiver_report.wireless)
                strcpy(app.status,"Wireless download interrupted. Check the network and JetBird App.");
            else strcpy(app.status,"USB download interrupted. Check the cable and JetBird App.");
            (void)psmtp_notify(app.status,app.stream);
        }
#ifndef PSMTP_TEST_EXPLORER
        if(app.usb_receiver_report.wireless && (app.direct_download!=2 || app.lan_revoked))psmtp_lan_forget();
        psmtp_usb_download_release(&app.usb_receiver_report);
#endif
        if(app.usb_receiver_report.wireless)app.lan_revoked=0;
    }
    if(!app.direct_download || !app.monitor_live || now<app.usb_receiver_retry)return;
    if(app.direct_download==1 && app.retained_phone)return;
    if(!app.usb_receiver)app.usb_receiver=psmtp_work_create();
    app.usb_receiver_report=(psmtp_usb_download_report){0};
    app.usb_receiver_report.wireless=app.direct_download==2;
    if(app.usb_receiver && !psmtp_work_start(app.usb_receiver,psmtp_usb_download_run,&app.usb_receiver_report,NULL)){
        app.usb_receiver_running=1;app.usb_progress_hidden=app.usb_receiver_report.wireless;app.usb_install_dispatched=0;
        app.usb_receive_meter=(psmtp_transfer_meter){0};
        app.usb_source_speed=0;app.usb_speed_time=0;
    }
    else app.usb_receiver_retry=now+2500;
}
#else
static void usb_receiver_tick(uint64_t now){(void)now;}
#endif

#ifndef PSMTP_TEST_EXPLORER
static void usb_progress_dialog(void){
    copy_ui ui={0};
    psmtp_work_progress_state state;
    if(!usb_transfer_snapshot(&state))return;
    ui.live=1;
    ui.panel.view=app.view;
    ui.panel.name=PSMTP_DOWNLOAD_DISPLAY;
    ui.panel.usb_download=1;
    ui.panel.wireless=app.usb_receiver_report.wireless;
    ui.panel.busy_epoch=++app.busy_epoch;
    ui.panel.transfer=app.usb_receive_meter;
    while(app.usb_receiver_running && app.direct_download && !app.exit_requested && !psmtp_screen_error()){
        uint64_t now=ui_now();
        if(now)usb_receiver_tick(now);
        if(!app.usb_receiver_running)break;
        (void)psmtp_work_snapshot(app.usb_receiver,&state);
        if(state.phase!=MTP_COPY_RECEIVE && state.phase!=MTP_COPY_PUBLISH)break;
        uint32_t buttons=read_buttons();
        if(app.exit_requested)break;
        if(buttons&ORBIS_PAD_BUTTON_CROSS){app.usb_progress_hidden=1;break;}
        if((buttons&(ORBIS_PAD_BUTTON_CIRCLE|ORBIS_PAD_BUTTON_OPTIONS)) && !psmtp_work_cancelled(app.usb_receiver))
            psmtp_work_cancel(app.usb_receiver);
        int cancel=psmtp_work_cancelled(app.usb_receiver);
        if(progress_due(&ui,state.phase,state.done,state.total,now,buttons,cancel)){
            progress_values(&ui,state.phase,state.done,state.total,now,buttons,cancel);
            ui.cancelling=cancel;
            draw_progress(&ui,now,cancel?"Cancelling download...":
                state.phase==MTP_COPY_PUBLISH?"Saving the verified file...":
                ui.panel.wireless?"Receiving from Android over the LAN.":"Receiving from Android over USB.");
        }
        (void)psmtp_work_wait(app.usb_receiver,4);
    }
    app.usb_receive_meter=ui.panel.transfer;
}
#endif

#ifndef PSMTP_TEST_EXPLORER
static int update_dialog_pump(void *unused){
    psmtp_console_selection panel={0};(void)unused;
    if(psmtp_ps4_exit_pending() || psmtp_screen_error())return 1;
    panel.view=PSMTP_VIEW_UPDATE;panel.notice_title="Software update";panel.notice_text="Choose Update or Cancel in the PlayStation dialog.";
    draw_ui(&app.ring,0,"Software update",1,app.scan_status,&panel,NULL);
    return 0; /* Native dialog owns controller input. */
}
static int update_work(void){
    psmtp_work_progress_state state={0};uint64_t drawn=0,exit_at=0;int rc,finished;
    if(!app.work)app.work=psmtp_work_create();if(!app.work)return MTP_MEMORY;
    rc=psmtp_work_start(app.work,psmtp_update_run,app.update_job,NULL);if(rc)return rc;app.copying=1;
    do {uint64_t now=ui_now();uint32_t buttons=read_buttons();unsigned epoch=0;int paused=0;
        psmtp_lifecycle_poll();psmtp_lifecycle_transfer_state(&paused,&epoch);
        psmtp_work_system(app.work,paused,epoch,psmtp_lifecycle_requested()!=0);work_logs();
        if(buttons&(ORBIS_PAD_BUTTON_CIRCLE|ORBIS_PAD_BUTTON_OPTIONS))psmtp_work_cancel(app.work);
        if(psmtp_ps4_exit_pending() || psmtp_screen_error()) {psmtp_work_shutdown(app.work);if(!exit_at)exit_at=now;if(now-exit_at>=5000)psmtp_ps4_exit(1,app.file);}
        if(!paused && (!drawn || now-drawn>=100)){psmtp_console_selection panel={0};(void)psmtp_work_snapshot(app.work,&state);
            panel.view=PSMTP_VIEW_UPDATE;panel.notice_title=app.update_job->download?"Downloading update":"Checking for updates";
            panel.notice_text=app.update_job->download?"The verified PKG will be saved in /data/pkg.":"Contacting Firebase for the latest PS4 release.";
            panel.done=state.done;panel.total=state.total;draw_ui(&app.ring,0,"Software update",1,app.scan_status,&panel,NULL);drawn=now;
        }
        finished=psmtp_work_done(app.work,&rc);if(!finished)(void)psmtp_work_wait(app.work,16);
    }while(!finished);
    if(psmtp_work_join(app.work,1000))psmtp_ps4_exit(1,app.file);work_logs();app.copying=0;
    (void)psmtp_screen_buttons();return rc;
}
static void check_for_updates(int manual){int rc,choice;char text[512];
    app.update_job=calloc(1,sizeof(*app.update_job));
    if(!app.update_job){if(manual)notice_dialog("Software update","There is not enough memory to check for updates.");return;}
    rc=update_work();
    if(psmtp_ps4_exit_pending() || psmtp_screen_error())goto done;
    if(rc || !psmtp_update_newer(&app.update_job->manifest,PSMTP_PACKAGE_VERSION)){
        if(manual && rc!=MTP_CANCELLED){
            if(rc==MTP_STATE)snprintf(text,sizeof(text),"The update connection is busy.\nTry again after the current transfer finishes.");
            else if(rc==MTP_NO_INTERNET)strcpy(text,"No internet connection");
            else if(rc)strcpy(text,"Could not check for updates.\nCheck your connection and the Firebase release configuration.");
            else snprintf(text,sizeof(text),"PS MTP Explorer %s is up to date.",PSMTP_VERSION);
            notice_dialog("Software update",text);
        }
        goto done;
    }
    preview_quiet();snprintf(text,sizeof(text),"PS MTP Explorer %s is available.\n\nDownload the update to /data/pkg?\nYour current installation will stay open.",app.update_job->manifest.version);
    choice=psmtp_system_dialog(text,1,update_dialog_pump,NULL,app.stream);(void)psmtp_screen_buttons();
    if(psmtp_system_dialog_busy())psmtp_ps4_exit(1,app.file);if(choice!=1 || psmtp_ps4_exit_pending())goto done;
    app.update_job->download=1;rc=update_work();if(psmtp_ps4_exit_pending())goto done;
    if(!rc)snprintf(text,sizeof(text),"Update downloaded and verified.\n\nSaved to:\n%s\n\nInstall it manually after closing the app.",app.update_job->saved_path);
    else if(rc==MTP_CANCELLED)snprintf(text,sizeof(text),"Update download cancelled.\nThe app will open normally.");
    else if(rc==MTP_EXISTS)snprintf(text,sizeof(text),"An update PKG with this name already exists in /data/pkg.\nIt has been preserved.\nThe app will open normally.");
    else if(rc==MTP_NO_INTERNET)strcpy(text,"No internet connection");
    else strcpy(text,"The update could not be downloaded or verified.\n\nCheck the connection and available storage.\nThe app will open normally.");
    (void)psmtp_system_dialog(text,0,update_dialog_pump,NULL,app.stream);(void)psmtp_screen_buttons();
    if(psmtp_system_dialog_busy())psmtp_ps4_exit(1,app.file);
done:
    free(app.update_job);app.update_job=NULL;
}
#endif
static int background_wait(copy_ui *ui,mtp_copy_progress present,int cancel_allowed){
    psmtp_work_progress_state state={0};uint64_t began=ui_now(),flushed=began,exit_at=0;int rc=0,finished;
    state.phase=MTP_COPY_PREPARE;ui->live=1;ui->cancelling=0;ui->frames=ui->progress_updates=0;ui->next_update=0;ui->max_gap=0;ui->panel.busy_epoch=++app.busy_epoch;
    do {
        uint64_t now=ui_now();int pause;unsigned epoch;
        psmtp_lifecycle_poll();psmtp_lifecycle_transfer_state(&pause,&epoch);
        psmtp_work_system(app.work,pause,epoch,psmtp_lifecycle_requested()!=0);
        work_logs();(void)psmtp_work_snapshot(app.work,&state);
        if(state.install && (!ui->frames || now>=ui->next_update || ui->panel.phase!=state.phase)){ui->panel.install_percent=state.report.percent;ui->panel.install_known=state.report.progress_known;ui->panel.install_preparing=state.report.preparing;}
        ui->cancelling=psmtp_work_cancelled(app.work);
        if(!pause && present(ui,(enum mtp_copy_phase)state.phase,state.done,state.total) && cancel_allowed)psmtp_work_cancel(app.work);
        finished=psmtp_work_done(app.work,&rc);
        if(psmtp_ps4_exit_pending()){
            psmtp_work_cancel(app.work);if(!exit_at)exit_at=now;
            /* Never return a borrowed context or close resources beneath an
             * uninterruptible call. Common cleanup retains a live worker. */
            if(!finished && now-exit_at>=5000)psmtp_ps4_exit(1,app.file);
        }
        if(now-flushed>=1000){if(app.file)fflush(app.file);if(app.scan_file)fflush(app.scan_file);flushed=now;}
        if(!finished){uint64_t after=ui_now(),delay=ui->next_present>after?ui->next_present-after:1;if(delay>4)delay=4;if(pause)delay=50;(void)psmtp_work_wait(app.work,(unsigned)delay);}
    }while(!finished);
    if(psmtp_work_join(app.work,1000))psmtp_ps4_exit(1,app.file);
    /* All remaining logs are bounded and the producer is joined. */
    {char bytes[8192];size_t n;while((n=psmtp_work_read_log(app.work,bytes,sizeof(bytes))))fwrite(bytes,1,n,app.stream);}
    ui->live=ui->cancelling=0;
    ui->panel.busy_epoch=0;
    fprintf(app.stream,"stage=operation-ui frames=%u elapsed-ms=%" PRIu64 " max-frame-gap-ms=%" PRIu64 " worker-status=%d clock=orbis-monotonic scope=worker-job progress-interval-ms=%u frame-target-ms=%u progress-updates=%u\n",ui->frames,ui_now()-began,ui->max_gap,rc,PROGRESS_UPDATE_MS,ui->panel.view==PSMTP_VIEW_INSTALL_PROGRESS?INSTALL_FRAME_MS:TRANSFER_FRAME_MS,ui->progress_updates);
    return rc;
}
static int background_call(copy_ui *ui,psmtp_work_fn fn,void *user,mtp_client *client,mtp_copy_progress present,int cancel_allowed){
    int rc;
    if(!app.work)app.work=psmtp_work_create();if(!app.work)return MTP_MEMORY;
    if(app.install_collecting){
        rc=background_wait(ui,present,cancel_allowed);app.install_collecting=0;
        if(rc || psmtp_work_cancelled(app.work))return rc?rc:MTP_CANCELLED;
    }
    rc=psmtp_work_start(app.work,fn,user,client);if(rc){work_logs();return rc;}
    return background_wait(ui,present,cancel_allowed);
}
#ifndef PSMTP_TEST_EXPLORER
typedef struct {
    psmtp_usb_install *offer;
    int retained, cleanup;
    psmtp_install_failure failure;
} usb_install_task;

static int usb_install_work(void *user,psmtp_work *work){
    usb_install_task *task=user;
    psmtp_usb_install *offer=task->offer;
    psmtp_install_session *session=NULL;
    psmtp_phone_source source;
    psmtp_install_report report={0};
    int rc,first_progress=1,first_copy=1;
    FILE *log=psmtp_work_log(work);
    do {
        if(psmtp_work_cancelled(work))return MTP_CANCELLED;
        rc=psmtp_usb_install_source(offer,&source);
        if(rc==1)sceKernelUsleep(20000);
    }while(rc==1);
    if(rc)return rc;
    rc=psmtp_install_prepare_stream(&session,psmtp_usb_install_name(offer),&source,
        psmtp_usb_install_retain,psmtp_usb_install_release,log);
    if(!rc && psmtp_work_cancelled(work))rc=MTP_CANCELLED;
    if(!rc)rc=psmtp_install_begin(session,1);
    if(!rc)fprintf(log,"stage=usb-direct-install-start received=%" PRIu64 " total=%" PRIu64 "\n",
        psmtp_usb_install_received(offer),source.size);
    while(!rc && !report.complete){
        if(psmtp_work_cancelled(work)){rc=MTP_CANCELLED;break;}
        rc=psmtp_install_poll(session,&report);
        if(rc)break;
        uint64_t received=psmtp_usb_install_received(offer);
        if(first_progress && report.done){
            first_progress=0;
            fprintf(log,"stage=usb-direct-install-native-progress received=%" PRIu64 " total=%" PRIu64 " native-bytes=%" PRIu64 " before-eof=%d\n",
                received,source.size,report.done,received<source.size);
        }
        if(first_copy && report.copy_percent>0 && report.copy_percent<=100){
            first_copy=0;
            fprintf(log,"stage=usb-direct-install-native-copy received=%" PRIu64 " total=%" PRIu64 " copy=%d before-eof=%d\n",
                received,source.size,report.copy_percent,received<source.size);
        }
        psmtp_work_install_progress(work,&report);
        if(!report.complete)sceKernelUsleep(250000);
    }
    if(rc)psmtp_install_get_failure(session,rc,&task->failure);
    int close_rc=psmtp_install_close_session(&session,rc!=0,&report.native_error);
    if(!task->failure.native_error)task->failure.native_error=report.native_error;
    fprintf(log,"stage=usb-direct-install-end status=%d cleanup=%d native=0x%08x retained=%d\n",
        rc,close_rc,report.native_error,session!=NULL);
    task->retained=session!=NULL;
    task->cleanup=close_rc;
    return rc?rc:close_rc;
}
static void usb_job_dialog(void){
    psmtp_usb_install *offer=psmtp_usb_install_acquire(&app.usb_receiver_report);
    if(!offer){usb_progress_dialog();return;}
    copy_ui ui={0};
    usb_install_task task={.offer=offer};
    ui.panel.view=PSMTP_VIEW_INSTALL_PROGRESS;
    ui.panel.usb_install=1;
    ui.panel.wireless=app.usb_receiver_report.wireless;
    ui.panel.name=psmtp_usb_install_name(offer);
    app.copying=1;app.progress_hidden=0;app.usb_install_dispatched=1;
    int rc=background_call(&ui,usb_install_work,&task,NULL,copy_progress,1);
    psmtp_usb_install_finish(offer,rc,task.cleanup,&task.failure);
    psmtp_usb_install_release(offer);
    if(task.retained)psmtp_ps4_exit(1,app.file);
    app.copying=0;
}
#endif
static int collect_installations(void *unused,psmtp_work *work){(void)unused;psmtp_install_collect(psmtp_work_log(work));return 0;}
static void install_idle_tick(uint64_t now){
    int rc=0;
    if(app.install_collecting){
        work_logs();if(!psmtp_work_done(app.work,&rc))return;
        if(psmtp_work_join(app.work,1000))psmtp_ps4_exit(1,app.file);
        work_logs();app.install_collecting=0;
        {psmtp_install_failure failure;if(psmtp_install_take_failure(&failure)){char notification[160];
            snprintf(notification,sizeof(notification),"%.9s: %s (0x%08x). Check PS4 Notifications.",failure.title_id,psmtp_install_failure_title(&failure),failure.native_error);
            snprintf(app.status,sizeof(app.status),"%s (0x%08x). Check Notifications.",psmtp_install_failure_title(&failure),failure.native_error);
            (void)psmtp_notify(notification,app.stream);
        }}
    }
    if(now-app.install_poll<1000 || !psmtp_install_background_pending())return;app.install_poll=now;
    if(!app.work)app.work=psmtp_work_create();if(!app.work)return;
    rc=psmtp_work_start(app.work,collect_installations,NULL,NULL);
    if(!rc)app.install_collecting=1;
}
static void copy_checkpoint(void) {
    diag_log_finish_line(&app.ring);
    if(app.file)fflush(app.file);
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
        if(!strncmp(p,"/data/psmtp/copies/",18) || !strncmp(p,PSMTP_DOWNLOAD_DIRECTORY "/",sizeof(PSMTP_DOWNLOAD_DIRECTORY)))return 1;
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
/* Idle-only polling: never share the MTP stream with previews or transfers. */
static void idle_refresh(void){
    struct timespec ts;uint64_t now;int changed=0,rc=0;
    if(app.copying || app.preview_pause || app.drawer || app.toolbar_focus || app.switch_requested || psmtp_preview_phone_busy() || psmtp_monotonic_time(&ts))return;
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
        if(buttons&ORBIS_PAD_BUTTON_CROSS){if(app.phone_current)rc=mtp_nav_sort(app.phone_current,(int)s.index);else if(app.local_current)psmtp_local_sort(app.local_current,(int)s.index);if(!rc){app.sort_mode=(int)s.index;save_preferences();}else strcpy(app.status,"Could not sort this folder. Refresh and retry.");break;}sceKernelUsleep(1000);
    }(void)psmtp_screen_buttons();
}
static void installation_mode_picker(void){psmtp_console_selection s={0};s.view=PSMTP_VIEW_INSTALL_MODE;s.index=(size_t)app.install_mode;(void)psmtp_screen_buttons();
    for(;;){uint32_t buttons;if(app.monitor_live)(void)poll_devices(0);draw_ui(&app.ring,0,"Choose how phone packages are installed.",1,app.scan_status,&s,NULL);if(psmtp_screen_error() || !psmtp_screen_pad_ready())break;
        buttons=read_buttons();if(buttons&(ORBIS_PAD_BUTTON_CIRCLE|ORBIS_PAD_BUTTON_OPTIONS))break;
        if(buttons&(ORBIS_PAD_BUTTON_LEFT|ORBIS_PAD_BUTTON_RIGHT))s.index=1-s.index;
        if((buttons&ORBIS_PAD_BUTTON_UP) && s.index)--s.index;if((buttons&ORBIS_PAD_BUTTON_DOWN) && s.index<1)++s.index;
        if(buttons&ORBIS_PAD_BUTTON_CROSS){app.install_mode=(int)s.index;save_preferences();fprintf(app.stream,"stage=settings installation-mode=%d default=fast persistence=file\n",app.install_mode);break;}(void)sceKernelUsleep(1000);
    }(void)psmtp_screen_buttons();
}
static void explorer_menu(int settings) {
    psmtp_console_selection menu={.theme_choice=app.theme};uint32_t buttons;if(app.copying)return;menu.view=settings?PSMTP_VIEW_SETTINGS:PSMTP_VIEW_MENU;(void)psmtp_screen_buttons();
    if(settings){app.settings_open=1;app.settings_index=0;}
    for(;;) {
        size_t count=settings?PSMTP_SETTINGS_COUNT:app.multi_mode?PSMTP_BATCH_COUNT:PSMTP_MENU_COUNT;
        if(usb_progress_pending())break;
        if(app.monitor_live) (void)poll_devices(0);
        if(app.phone_current && app.phone_detached && !settings) break;
        if(menu.index>=count) menu.index=count-1;
        if(settings)app.settings_index=(int)menu.index;
        draw_ui(&app.ring,0,settings?app.status:"Options",1,app.scan_status,&menu,NULL);
        if(psmtp_screen_error() || !psmtp_screen_pad_ready()) break;
        buttons=read_buttons();if(buttons&(ORBIS_PAD_BUTTON_CIRCLE|ORBIS_PAD_BUTTON_OPTIONS|ORBIS_PAD_BUTTON_TRIANGLE)) break;
        if((buttons&ORBIS_PAD_BUTTON_UP) && menu.index){
            --menu.index;
            if(settings && !app.direct_download && menu.index==PSMTP_SETTINGS_DOWNLOAD_MODE)--menu.index;
        }
        if((buttons&ORBIS_PAD_BUTTON_DOWN) && menu.index+1<count){
            ++menu.index;
            if(settings && !app.direct_download && menu.index==PSMTP_SETTINGS_DOWNLOAD_MODE)++menu.index;
        }
        if(settings)app.settings_index=(int)menu.index;
        if(menu.index!=PSMTP_SETTINGS_THEME)menu.theme_choice=app.theme;
        if(settings && menu.index==PSMTP_SETTINGS_THEME && (buttons&(ORBIS_PAD_BUTTON_LEFT|ORBIS_PAD_BUTTON_RIGHT))){
            int offset=buttons&ORBIS_PAD_BUTTON_LEFT?-1:1;
            menu.theme_choice=(menu.theme_choice+offset+PSMTP_THEME_COUNT)%PSMTP_THEME_COUNT;
        }
        if(settings && menu.index==PSMTP_SETTINGS_INSTALL && (buttons&(ORBIS_PAD_BUTTON_LEFT|ORBIS_PAD_BUTTON_RIGHT))){
            app.install_mode=1-app.install_mode;save_preferences();
            fprintf(app.stream,"stage=settings installation-mode=%d default=fast persistence=file input=left-right\n",app.install_mode);continue;
        }
        if(settings && menu.index==PSMTP_SETTINGS_DOWNLOAD_MODE && (buttons&(ORBIS_PAD_BUTTON_LEFT|ORBIS_PAD_BUTTON_RIGHT))){
            int mode=(buttons&ORBIS_PAD_BUTTON_LEFT)?1:2;
            if(mode!=app.direct_download)receiver_mode(mode);
            continue;
        }
        if(buttons&ORBIS_PAD_BUTTON_CROSS) {
                if(settings && menu.index==PSMTP_SETTINGS_RESET){restore_defaults();continue;}
                if(settings && menu.index==PSMTP_SETTINGS_INSTALL){installation_mode_picker();continue;}
                if(settings && menu.index==PSMTP_SETTINGS_THEME){app.theme=menu.theme_choice;save_preferences();continue;}
                if(settings && menu.index==PSMTP_SETTINGS_UPDATE){
#ifndef PSMTP_TEST_EXPLORER
                    check_for_updates(1);
#endif
                    continue;
                }
                if(settings && menu.index==PSMTP_SETTINGS_DOWNLOAD){
                    receiver_mode(app.direct_download?0:app.download_mode?app.download_mode:1);
                    fprintf(app.stream,"stage=settings direct-download=%d scope=session\n",app.direct_download);
                    continue;
                }
                if(settings && menu.index==PSMTP_SETTINGS_DOWNLOAD_MODE){
                    psmtp_lan_pair_ui *pairing=&app.usb_receiver_report.pairing;
                    if(app.direct_download==2 && __atomic_load_n(&pairing->code,__ATOMIC_ACQUIRE)){
                        int pending=1;
                        (void)__atomic_compare_exchange_n(&pairing->decision,&pending,2,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
                        continue;
                    }
                    receiver_mode(app.direct_download==1?2:1);
                    continue;
                }
                if(settings){int enabled=!psmtp_stream_boost_get();psmtp_stream_boost_set(enabled);save_preferences();
                    fprintf(app.stream,"stage=settings speed-boost=%d applies=next-transfer persistence=file initial-default=off\n",enabled);continue;}
                if(app.multi_mode){if(!app.marked_count)continue;batch_action(menu.index);break;}
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
                else if(menu.index==PSMTP_MENU_EXTRACT || menu.index==PSMTP_MENU_COMPRESS)archive_selection(menu.index==PSMTP_MENU_EXTRACT);
            break;
        }
        sceKernelUsleep(1000);
    }
    if(settings)app.settings_open=0;
    (void)psmtp_screen_buttons();
}
static size_t selected_index(void) {return app.phone_current?app.phone_current->selected:app.local_current?app.local_current->selected:0;}
static uint32_t file_buttons(void) {uint32_t buttons=read_buttons();if(app.open_requested) {app.open_requested=0;buttons|=ORBIS_PAD_BUTTON_CROSS;}return buttons;}
static int shared_controls(uint32_t buttons) {
    if(app.exit_requested)return 1;
    multi_check_scope();
    if(app.multi_mode && (buttons&ORBIS_PAD_BUTTON_CIRCLE)){multi_end();strcpy(app.status,"Selection cleared.");return 1;}
    if(!app.drawer && !app.toolbar_focus && !app.copying &&
       (buttons&ORBIS_PAD_BUTTON_SQUARE || (app.multi_mode && buttons&ORBIS_PAD_BUTTON_CROSS))){multi_toggle();return 1;}
    if((buttons&ORBIS_PAD_BUTTON_R3) && app.usb_progress_hidden && app.direct_download){
        psmtp_work_progress_state progress;
        if(usb_transfer_snapshot(&progress)){app.usb_progress_hidden=0;return 1;}
    }
    if(!app.drawer && app.toolbar_focus){
        if(buttons&(ORBIS_PAD_BUTTON_DOWN|ORBIS_PAD_BUTTON_CIRCLE)){app.toolbar_focus=0;return 1;}
        if(buttons&ORBIS_PAD_BUTTON_LEFT){if(app.toolbar_focus>1)--app.toolbar_focus;else {app.toolbar_focus=0;app.drawer=1;app.locations.selected=app.locations.active;}return 1;}
        if(buttons&ORBIS_PAD_BUTTON_RIGHT){if(app.toolbar_focus<3)++app.toolbar_focus;return 1;}
        if(buttons&ORBIS_PAD_BUTTON_CROSS){if(app.toolbar_focus<3){app.list_view=app.toolbar_focus==2;save_preferences();}else sort_picker();return 1;}
        return 1;
    }
    if(!app.drawer && (buttons&ORBIS_PAD_BUTTON_UP) && selected_index()<(app.list_view?1u:4u)){app.toolbar_focus=app.list_view?2:1;return 1;}
    if(buttons&ORBIS_PAD_BUTTON_L3) {app.list_view=!app.list_view;save_preferences();return 1;}
    if(buttons&ORBIS_PAD_BUTTON_OPTIONS) {if(!(app.drawer && app.drawer_settings))explorer_menu(0);return 1;}
    if(!app.drawer && (buttons&ORBIS_PAD_BUTTON_TRIANGLE)) {explorer_menu(0);return 1;}
    if(buttons&ORBIS_PAD_BUTTON_TOUCH_PAD) {if(app.drawer) app.refresh_requested=1;else refresh_folder();return 1;}
    if((buttons&ORBIS_PAD_BUTTON_LEFT) && !app.drawer && (app.list_view || selected_index()%4==0)) {app.drawer=1;app.drawer_settings=0;app.locations.selected=app.locations.active;return 1;}
    if(!app.drawer) return 0;
    if(app.drawer_settings && (buttons&ORBIS_PAD_BUTTON_RIGHT))return 1;
    if(app.drawer_settings && (buttons&ORBIS_PAD_BUTTON_CROSS)) {explorer_menu(1);return 1;}
    if(buttons&(ORBIS_PAD_BUTTON_RIGHT|ORBIS_PAD_BUTTON_CROSS|ORBIS_PAD_BUTTON_CIRCLE)) {app.drawer=app.drawer_settings=0;return 1;}
    if(buttons&ORBIS_PAD_BUTTON_TRIANGLE) app.refresh_requested=1;
    if(buttons&ORBIS_PAD_BUTTON_UP) {if(app.drawer_settings) app.drawer_settings=0;else if(psmtp_locations_move(&app.locations,-1)) app.switch_requested=1;}
    if(buttons&ORBIS_PAD_BUTTON_DOWN) {if(!app.drawer_settings && app.locations.selected+1>=app.locations.count) {app.drawer_settings=1;return 1;}else if(!app.drawer_settings && psmtp_locations_move(&app.locations,1)) app.switch_requested=1;}
    if(app.switch_requested) {multi_end();app.target=app.locations.entries[app.locations.selected];}
    return 1;
}
static int usb_target_missing(void) {
    return app.target.kind==PSMTP_LOCATION_USB && !(app.usb_mask&(1u<<app.target.slot));
}
static int poll_devices(int force) {
    if(psmtp_ps4_exit_pending())app.exit_requested=1;
    struct timespec ts={0};uint64_t now;unsigned usb;psmtp_ps4_candidate phones[128]={{0}};size_t n=0;int rc,changed;
    if(app.shutting_down || app.exit_requested || app.copying || psmtp_preview_phone_busy() || psmtp_monotonic_time(&ts)) return usb_target_missing();
    now=(uint64_t)ts.tv_sec*1000+(uint64_t)ts.tv_nsec/1000000;
    install_idle_tick(now);
    if(!force && now-app.mount_probe<PSMTP_DEVICE_POLL_MS) return usb_target_missing();
    app.mount_probe=now;
#ifndef PSMTP_TEST_EXPLORER
    if(app.media_retiring && !psmtp_media_close_poll(app.media_player,0)){
        app.media_player=NULL;app.media_retiring=0;psmtp_audio_open(app.stream);
    }
#endif
    rc=accessory_owns_usb()?0:psmtp_ps4_poll_devices(phones,&n,force,app.stream);
    psmtp_audio_status(app.stream);
    if(rc) {
        if(!app.poll_failed) fprintf(app.stream,"stage=device-watch failed status=%d; keeping last phone inventory\n",rc);
        n=app.phone_count;memcpy(phones,app.phones,n*sizeof(*phones));
    } else if(app.poll_failed) fputs("stage=device-watch recovered=1\n",app.stream);
    app.poll_failed=rc!=0;
    usb=psmtp_local_usb_mask();
    if(!psmtp_monotonic_time(&ts)) now=(uint64_t)ts.tv_sec*1000+(uint64_t)ts.tv_nsec/1000000;
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
    if(psmtp_screen_error() || (buttons&(ORBIS_PAD_BUTTON_CIRCLE|ORBIS_PAD_BUTTON_OPTIONS))) return 1;
    now=psmtp_monotonic_time(&ts)?app.last_draw+100:(uint64_t)ts.tv_sec*1000+(uint64_t)ts.tv_nsec/1000000;
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

static void multi_check_scope(void){
    if(!app.multi_mode)return;
    if(!psmtp_location_same(&app.multi_location,&app.target) || app.phone_detached){multi_end();return;}
    if(app.phone_current){
        if(app.multi_parent!=app.phone_current->parent || app.multi_storage!=app.phone_current->storages[app.phone_current->storage_index])multi_end();
    }else if(!app.local_current || strcmp(app.multi_path,app.local_current->path))multi_end();
}

static int multi_index(uint32_t handle,const char *path,const char *name){
    for(size_t i=0;i<app.marked_count;++i){
        const psmtp_op_ref *ref=&app.marked[i];
        if(handle?ref->handle==handle && !strcmp(ref->name,name):!ref->handle && !strcmp(ref->path,path))return (int)i;
    }
    return -1;
}

static int multi_marked(size_t index){
    if(app.phone_current){
        const mtp_navigator *nav=app.phone_current;
        if(index>=nav->count)return 0;
        const mtp_nav_entry *entry=nav->catalog?&nav->catalog[index]:
            index>=nav->page_start && index-nav->page_start<nav->page_count?&nav->entries[index-nav->page_start]:NULL;
        return entry && multi_index(entry->handle,NULL,entry->info.name)>=0;
    }
    const psmtp_local_browser *local=app.local_current;
    return local && index<local->count && multi_index(0,local->entries[index].path,NULL)>=0;
}

static void multi_toggle(void){
    psmtp_op_ref source;
    int rc=capture_selection(&source);
    if(rc){strcpy(app.status,"Highlight an available file or folder to select it.");return;}
    if(!app.multi_mode){
        app.multi_location=app.target;app.multi_mode=1;
        if(app.phone_current){
            app.multi_parent=app.phone_current->parent;
            app.multi_storage=app.phone_current->storages[app.phone_current->storage_index];
        }else strcpy(app.multi_path,app.local_current->path);
    }
    int at=multi_index(source.handle,source.path,source.name);
    if(at>=0){
        --app.marked_count;
        memmove(&app.marked[at],&app.marked[at+1],(app.marked_count-(size_t)at)*sizeof(*app.marked));
    }else if(app.marked_count<MULTI_LIMIT)app.marked[app.marked_count++]=source;
    else {strcpy(app.status,"Selection limit reached. Select up to 128 items per action.");return;}
    app.status[0]=0;
}
static void notice_dialog(const char *title,const char *text){
    psmtp_console_selection p={0};p.view=PSMTP_VIEW_NOTICE;p.notice_title=title;p.notice_text=text;(void)psmtp_screen_buttons();
    for(;;){draw_ui(&app.ring,0,title,1,app.scan_status,&p,NULL);if(psmtp_screen_error() || !psmtp_screen_pad_ready() || (read_buttons()&(ORBIS_PAD_BUTTON_CROSS|ORBIS_PAD_BUTTON_CIRCLE|ORBIS_PAD_BUTTON_OPTIONS)))break;sceKernelUsleep(1000);}
    (void)psmtp_screen_buttons();
}
typedef struct {
    int step,cancel,previous_rc,cleanup_rc,direct;psmtp_install_session **session;
    const psmtp_op_ref *source;mtp_client *client;psmtp_pkg_info *info;psmtp_install_report *report;
} install_work;
static int install_background(void *v,psmtp_work *w){
    install_work *t=v;int rc=0;
    if(t->step!=3 && psmtp_work_cancelled(w))return MTP_CANCELLED;
    if(t->step==0){t->report->failure.step=PSMTP_INSTALL_INSPECT;return psmtp_install_prepare(t->session,t->source,t->client,t->info,psmtp_work_log(w));}
    if(t->step==1){rc=psmtp_install_stage(t->session,1,psmtp_work_progress,w);if(rc)psmtp_install_get_failure(*t->session,rc,&t->report->failure);return rc;}
    if(t->step==2){
        if(t->direct){rc=psmtp_install_phone_direct(*t->session,psmtp_work_source_state,w);if(rc){psmtp_install_get_failure(*t->session,rc,&t->report->failure);return rc;}}
        rc=psmtp_install_begin(*t->session,1);
        if(rc){psmtp_install_get_failure(*t->session,rc,&t->report->failure);return rc;}
        /* Keep ownership until terminal progress, as in 1.01. Native calls
         * stay on the worker so the UI can still process cancel/OS close. */
        for(;;){
            if(psmtp_work_cancelled(w))return MTP_CANCELLED;
            rc=psmtp_install_poll(*t->session,t->report);
            if(rc){psmtp_install_get_failure(*t->session,rc,&t->report->failure);return rc;}
            psmtp_work_install_progress(w,t->report);
            if(t->report->complete)return 0;
            if(psmtp_work_cancelled(w))return MTP_CANCELLED;
            (void)sceKernelUsleep(INSTALL_FRAME_MS*1000u);
        }
    }
    if(*t->session && t->previous_rc)psmtp_install_get_failure(*t->session,t->previous_rc,&t->report->failure);
    if(!t->report->native_error)t->report->native_error=psmtp_install_native_error(*t->session);
    if(!t->previous_rc && t->report->complete)t->cleanup_rc=psmtp_install_cleanup(*t->session);
    return psmtp_install_close_session(t->session,t->cancel || t->previous_rc!=0,&t->report->native_error);
}
static int install_selection(void){
    psmtp_op_ref source;psmtp_pkg_info info;psmtp_install_session *session=NULL;psmtp_install_report report={0};copy_ui ui={0};int rc,close_rc,cleanup_rc=0;char error[384];uint32_t buttons;
    install_work task={0};task.session=&session;task.source=&source;task.client=app.phone_current?app.phone_current->client:NULL;task.info=&info;task.report=&report;
    task.direct=task.client && app.install_mode==PSMTP_INSTALL_PHONE_DIRECT;
    preview_quiet();rc=capture_selection(&source);if(rc)return rc;app.copying=1;app.progress_hidden=0;
    ui.panel.name=source.name;ui.panel.view=PSMTP_VIEW_TRANSFERS;
    rc=background_call(&ui,install_background,&task,task.client,copy_progress,1);if(rc)goto done;
    ui.panel.view=PSMTP_VIEW_INSTALL_CONFIRM;ui.panel.action=task.direct?5:source.location.kind==PSMTP_LOCATION_PHONE?3:0;ui.panel.name=source.name;ui.panel.source_total=info.size;ui.panel.destination=source.location.label;ui.panel.notice_text=info.content_id;
    (void)psmtp_screen_buttons();
    for(;;){draw_ui(&app.ring,0,"Confirm package installation.",1,app.scan_status,&ui.panel,NULL);
        if(psmtp_screen_error() || !psmtp_screen_pad_ready()){rc=MTP_CANCELLED;goto done;}buttons=read_buttons();
        if(buttons&(ORBIS_PAD_BUTTON_CIRCLE|ORBIS_PAD_BUTTON_OPTIONS)){rc=MTP_CANCELLED;goto done;}if(buttons&ORBIS_PAD_BUTTON_CROSS)break;sceKernelUsleep(1000);}
    (void)psmtp_screen_buttons();
    if(source.location.kind==PSMTP_LOCATION_PHONE && !task.direct){
        ui.panel.view=PSMTP_VIEW_TRANSFERS;ui.panel.copy_mode=2;ui.panel.phase=MTP_COPY_RECEIVE;ui.panel.total=info.size;ui.panel.done=0;
        transfer_notice(0,0,0);
        task.step=1;rc=background_call(&ui,install_background,&task,task.client,copy_progress,1);transfer_notice(0,rc,1);if(rc)goto done;
        memset(&ui.panel.transfer,0,sizeof(ui.panel.transfer));ui.started=ui.drawn=0;ui.timing_phase=0;app.progress_hidden=0;
    }
    /* Transfers have finished. Keep the installation screen and session
     * owned here until native completion, failure or explicit cancellation. */
    app.progress_hidden=0;ui.panel.view=PSMTP_VIEW_INSTALL_PROGRESS;ui.panel.copy_mode=2;
    ui.panel.phase=MTP_COPY_RECEIVE;ui.panel.total=ui.panel.done=0;
    ui.panel.install_percent=ui.panel.install_known=ui.panel.install_preparing=0;
    task.step=2;rc=background_call(&ui,install_background,&task,NULL,copy_progress,1);
    if(!rc && !report.complete)rc=MTP_STATE;
done:
    task.step=3;task.previous_rc=rc;task.cancel=rc!=0;
    close_rc=session?background_call(&ui,install_background,&task,NULL,copy_progress,0):0;cleanup_rc=task.cleanup_rc;
    /* Failure to schedule cleanup must not leave an installer task running
     * while normal browsing resumes. Common exit cleanup owns every session. */
    if(session){fprintf(app.stream,"stage=install-cleanup-worker-unavailable status=%d safe-exit=1\n",close_rc);psmtp_ps4_exit(1,app.file);}
    if(close_rc){if(!rc && report.complete)cleanup_rc=close_rc;else rc=close_rc;}
    if(!rc){
        notice_dialog("Installation successful","PKG installed successfully.");
        strcpy(app.status,"Package installed successfully.");
    }
    else if(!report.native_error && rc==MTP_CANCELLED)strcpy(app.status,"Installation cancelled. Source PKG preserved.");
    else if(task.direct && !report.native_error && rc==MTP_UNSUPPORTED){notice_dialog("Direct installation unavailable","Direct mode needs compatible phone reads\nand system support. Select Fast Installation\nin Settings, then retry. Source PKG preserved.");strcpy(app.status,"Direct mode unavailable. Select Fast Installation in Settings.");}
    else if(!report.native_error && rc==MTP_NO_SPACE){notice_dialog("Insufficient storage space",task.direct?"Free space for the installed content and\nsystem installer data is required.\nSource PKG preserved.":"Free space for both the full saved PKG\nand the installed content is required.\nSource PKG preserved.");strcpy(app.status,"Not enough PS4 storage for installation. Source PKG preserved.");}
    else {
        report.failure.app_error=rc;report.failure.native_error=report.native_error;
        psmtp_install_failure_text(&report.failure,error,sizeof(error));notice_dialog(psmtp_install_failure_title(&report.failure),error);
        strcpy(app.status,"Installation failed. Source PKG preserved; check PS4 Notifications.");
    }
    fprintf(app.stream,"stage=install-selection status=%d native=0x%08x original-source-removed=0 cleanup-warning=%d\n",rc,report.native_error,cleanup_rc);app.copying=0;copy_checkpoint();(void)psmtp_screen_buttons();return rc;
}
static int ime_pump(void *unused){
    psmtp_console_selection background={0};const char *error=unused;
    if(psmtp_ps4_exit_pending())return 1;
    if(psmtp_ime_busy())return psmtp_screen_error()!=0;
    background.view=app.view;draw_ui(&app.ring,0,error && *error?error:"Enter a name using the PlayStation keyboard.",1,app.scan_status,&background,app.phone_current);
    (void)psmtp_screen_buttons();
    return psmtp_screen_error() || !psmtp_screen_pad_connected();
}
static void edit_selection_name(int creating) {
    psmtp_op_ref source;char name[256],result[256],error[128]="";int rc;
    preview_quiet();memset(&source,0,sizeof(source));rc=creating?0:capture_selection(&source);if(rc){strcpy(app.status,"Select an item to rename.");return;}
    snprintf(name,sizeof(name),"%s",creating?"New Folder":source.name);(void)psmtp_screen_buttons();
    app.preview_pause=1;
    while(psmtp_screen_buttons_down()&(ORBIS_PAD_BUTTON_CROSS|ORBIS_PAD_BUTTON_CIRCLE|ORBIS_PAD_BUTTON_OPTIONS|ORBIS_PAD_BUTTON_TRIANGLE)){
        if(ime_pump(error)){app.preview_pause=0;return;}sceKernelUsleep(16000);
    }
    for(;;){
        rc=psmtp_ime_text(error[0]?"Check the name and try again":creating?"New Folder":"Rename",name,sizeof(name),ime_pump,error,app.stream);
        if(psmtp_ime_busy())psmtp_ps4_exit(1,app.file);
        if(rc==MTP_LIMIT){strcpy(error,"Name is too long. Enter a shorter name.");continue;}
        if(rc){if(rc!=MTP_CANCELLED)strcpy(app.status,"PlayStation keyboard unavailable. Close the app and retry.");break;}
        if(!psmtp_name_valid(name)){strcpy(error,"Enter a valid name. Keep the extension if needed.");continue;}
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

    }
    app.preview_pause=0;(void)psmtp_screen_buttons();
}
static void rename_selection(void){edit_selection_name(0);}
static void new_folder(void){edit_selection_name(1);}
static int operation_progress(void *context,enum mtp_copy_phase phase,uint64_t done,uint64_t total) {
    copy_ui *ui=context;struct timespec ts={0};uint64_t now;uint32_t buttons;
    if(ui->live && psmtp_work_pause_requested(app.work))return 0;
    buttons=read_buttons();
    int cancel=psmtp_screen_error() || (buttons&(ORBIS_PAD_BUTTON_CIRCLE|ORBIS_PAD_BUTTON_OPTIONS));
    if(cancel && !ui->live)return 1;
    if(buttons&ORBIS_PAD_BUTTON_CROSS) app.progress_hidden=!app.progress_hidden;
    now=psmtp_monotonic_time(&ts)?ui->drawn+100:(uint64_t)ts.tv_sec*1000+(uint64_t)ts.tv_nsec/1000000;
    if(progress_due(ui,(int)phase,done,total,now,buttons,cancel)) {
        progress_values(ui,(int)phase,done,total,now,buttons,cancel);
        draw_progress(ui,now,ui->cancelling || cancel?"Cancelling; waiting for the current operation...":phase==MTP_COPY_LOCAL?"Verifying...":"Working on your selection. Circle cancels between completed operations.");ui->drawn=now;
    }
    return cancel;
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
typedef struct {
    psmtp_op_plan *plan;const psmtp_op_ref *source;mtp_client *client;const psmtp_location *destination;
    const char *path;uint32_t storage,parent;int step,action;
} operation_work;
static int operation_background(void *v,psmtp_work *w){
    operation_work *t=v;
    if(psmtp_work_cancelled(w))return MTP_CANCELLED;
    if(t->step==0)return t->action==2?
        psmtp_op_plan_delete(t->plan,t->source,t->client,psmtp_work_progress,w,psmtp_work_log(w)):
        psmtp_op_plan_build(t->plan,t->source,t->client,psmtp_work_progress,w,psmtp_work_log(w));
    if(t->step==1){int rc;
        rc=t->destination->kind==PSMTP_LOCATION_PHONE?psmtp_op_target_phone(t->plan,t->client,t->destination,t->storage,t->parent):psmtp_op_target(t->plan,t->destination,t->path);
        return rc?rc:psmtp_op_space(t->plan,t->client);
    }
    return t->action==2?psmtp_op_delete(t->plan,t->client,0,psmtp_work_progress,w):psmtp_op_execute(t->plan,t->client,t->action==1,psmtp_work_progress,w);
}
static void run_operation(const psmtp_op_ref *source,mtp_client *client,int action,const psmtp_location *destination,const char *path) {
    psmtp_op_plan *plan=calloc(1,sizeof(*plan));copy_ui ui={0};int rc,transfer_started=0;uint64_t confirmed_at=0;
    operation_work task={0};task.plan=plan;task.source=source;task.client=client;task.action=action;task.destination=destination;task.path=path;
    if(!plan) {strcpy(app.status,"Not enough memory for this operation.");return;}
    app.copying=1;app.progress_hidden=0;ui.panel.name=source->name;ui.panel.folder=source->folder;ui.panel.operation=1;ui.panel.action=action;
    (void)psmtp_screen_buttons();
    preview_quiet();rc=background_call(&ui,operation_background,&task,client,operation_progress,1);
    if(!rc && action!=2) {
        if(destination->kind==PSMTP_LOCATION_PHONE && app.phone_current){task.storage=app.phone_current->storages[app.phone_current->storage_index];task.parent=app.phone_current->parent;}
        task.step=1;rc=background_call(&ui,operation_background,&task,client,operation_progress,1);
    }
    if(!rc && app.paste_pending && (app.paste_stat.st_dev!=plan->destination_stat.st_dev || app.paste_stat.st_ino!=plan->destination_stat.st_ino)) rc=MTP_VERIFY;
    if(!rc && action==2 && source->location.kind==PSMTP_LOCATION_PHONE && !plan->delete_supported) rc=MTP_UNSUPPORTED;
    if(rc==MTP_NO_SPACE || rc==MTP_SPACE_UNKNOWN)transfer_warning(rc);
    if(!rc) rc=operation_confirm(plan,&ui,action);
    if(!rc) {
        confirmed_at=ui_now();
        transfer_started=1;transfer_notice(action,0,0);
        ui.panel.copy_mode=2;ui.panel.view=PSMTP_VIEW_PHONE;
        task.step=2;rc=background_call(&ui,operation_background,&task,client,operation_progress,1);
    }
    if(plan->uncertain_upload)strcpy(app.status,"Phone upload interrupted; a partial file may remain. Reconnect and inspect the destination before retrying.");
    else if(plan->uncertain_delete) strcpy(app.status,"Phone deletion interrupted; source state is uncertain. Check both devices before retrying.");
    else if(rc==MTP_DISCONNECTED)strcpy(app.status,"Device changed or was removed while paused. Transfer stopped; check both locations.");
    else if(rc==MTP_CANCELLED) snprintf(app.status,sizeof(app.status),"Cancelled: %zu files copied, %zu source items removed.",plan->copied,plan->removed);
    else if(rc==MTP_NO_SPACE)strcpy(app.status,"Insufficient storage space. Transfer stopped; source items are preserved.");
    else if(rc==MTP_EXISTS)strcpy(app.status,"An item with this name already exists. Rename the source or choose another folder.");
    else if(rc==MTP_UNSUPPORTED)strcpy(app.status,"This device is read-only or does not support the selected operation.");
    else if(rc==MTP_LIMIT)strcpy(app.status,"Selection exceeds supported transfer limits. Choose a smaller file or folder.");
    else if(rc==MTP_SPACE_UNKNOWN)strcpy(app.status,"Available storage could not be checked. Transfer blocked.");
    else if(rc && action==2)snprintf(app.status,sizeof(app.status),"Delete stopped (%d): %zu items removed. Refresh to check remaining items.",rc,plan->removed);
    else if(rc==MTP_FILESYSTEM && plan->error_stage)snprintf(app.status,sizeof(app.status),"Destination access failed (error %d). Check storage access.",plan->error_number);
    else if(rc) snprintf(app.status,sizeof(app.status),"Operation stopped (%d): %zu files copied, %zu items removed.",rc,plan->copied,plan->removed);
    else if(action==2) snprintf(app.status,sizeof(app.status),source->folder?"Folder and contents deleted (%zu items).":"Deleted %zu selected items.",plan->removed);
    else snprintf(app.status,sizeof(app.status),action==1?"Move verified: %zu files copied, %zu source items removed.":"Paste complete: %zu files copied and verified.",plan->copied,plan->removed);
    if(plan->cleanup_failed && !rc) strcpy(app.status,"Operation completed. Some temporary files could not be removed.");
    if((action==1 && (!rc || plan->removed || plan->uncertain_delete)) || action==2) app.clipboard_active=0;
    if(client && app.retained_phone && client==app.retained_phone->client && (plan->removed || plan->uncertain_delete || plan->destination_location.kind==PSMTP_LOCATION_PHONE)) app.phone_dirty=1;
    fprintf(app.stream,"stage=clipboard-operation action=%d status=%d files=%zu removed=%zu cleanup-warning=%d names-in-log=0\n",action,rc,plan->copied,plan->removed,plan->cleanup_failed);
    app.copying=0;copy_checkpoint();
    if(confirmed_at)fprintf(app.stream,"stage=operation-complete-timing elapsed-ms=%" PRIu64 " planned-bytes=%" PRIu64 " copied=%zu status=%d clock=orbis-monotonic scope=confirm-through-checkpoint\n",ui_now()-confirmed_at,plan->bytes,plan->copied,rc);
    if(transfer_started)transfer_notice(action,rc,1);
    free(plan);(void)psmtp_screen_buttons();
}
static void reload_local(void) {
    char path[256];if(!app.local_current || app.view!=PSMTP_VIEW_LOCAL) return;
    strcpy(path,app.local_current->path);local_result(psmtp_local_load(app.local_current,path),app.local_current);
}
static int confirm_action(const char *title,const char *message){
    psmtp_console_selection panel={0};
    panel.view=PSMTP_VIEW_CONFIRM;panel.notice_title=title;panel.notice_text=message;
    (void)psmtp_screen_buttons();
    for(;;){
        draw_ui(&app.ring,0,title,1,app.scan_status,&panel,NULL);
        if(psmtp_screen_error() || !psmtp_screen_pad_ready() || psmtp_ps4_exit_pending())return 0;
        uint32_t buttons=read_buttons();
        if(buttons&(ORBIS_PAD_BUTTON_CIRCLE|ORBIS_PAD_BUTTON_OPTIONS))return 0;
        if(buttons&ORBIS_PAD_BUTTON_CROSS)return 1;
        sceKernelUsleep(1000);
    }
}

static int viewer_keyboard_pump(void *unused){
    (void)unused;
    return psmtp_ps4_exit_pending() || psmtp_screen_error() || !psmtp_screen_pad_connected();
}

static int wait_for_key_release(uint32_t mask){
    for(;;){
        (void)psmtp_screen_buttons();
        if(viewer_keyboard_pump(NULL))return MTP_CANCELLED;
        if(!(psmtp_screen_buttons_down()&mask))return 0;
        sceKernelUsleep(16000);
    }
}

#ifndef PSMTP_TEST_EXPLORER
static int prompt_file_text(const char *,char [256],int);

static int viewer_edit_line(psmtp_viewer *viewer){
    char text[256];
    psmtp_viewer_line(viewer,viewer->line,viewer->column,text,240);
    if(wait_for_key_release(ORBIS_PAD_BUTTON_CROSS))return MTP_CANCELLED;
    int rc=psmtp_ime_text("Edit line",text,sizeof(text),viewer_keyboard_pump,NULL,app.stream);
    if(psmtp_ime_busy())psmtp_ps4_exit(1,app.file);
    (void)psmtp_screen_buttons();
    return rc?rc:psmtp_viewer_edit(viewer,text,0);
}

typedef struct {const psmtp_viewer *viewer;const char *name;} text_copy_task;
static int save_phone_text_work(void *context,psmtp_work *work){
    const text_copy_task *task=context;
    const psmtp_viewer *v=task->viewer;
    psmtp_op_ref local;
    psmtp_local_entry entry={0};
    psmtp_location internal={.kind=PSMTP_LOCATION_INTERNAL,.root="/data"};
    strcpy(entry.path,v->staged_path);strcpy(entry.name,task->name);
    int rc=psmtp_op_capture(&local,&internal,&entry);
    if(!rc)rc=psmtp_op_replace_text(&local,v->text,v->length);
    if(rc)return rc;
    psmtp_op_plan *plan=calloc(1,sizeof(*plan));
    if(!plan)return MTP_MEMORY;
    rc=psmtp_op_plan_build(plan,&local,NULL,psmtp_work_progress,work,psmtp_work_log(work));
    if(!rc)rc=psmtp_op_target_phone(plan,v->client,&v->source.location,v->source.info.storage,
        v->source.info.parent?v->source.info.parent:MTP_ROOT);
    if(!rc)rc=psmtp_op_space(plan,v->client);
    if(!rc)rc=psmtp_op_execute(plan,v->client,0,psmtp_work_progress,work);
    free(plan);
    return rc;
}

static int save_viewer_text(psmtp_viewer *viewer){
    if(!viewer->client)return psmtp_op_replace_text(&viewer->source,viewer->text,viewer->length);
    char name[256];
    const char *source=viewer->source.name,*extension=strrchr(source,'.');
    size_t length=extension?(size_t)(extension-source):strlen(source);
    if(length>180)length=180;
    while(((unsigned char)source[length]&0xc0)==0x80)--length;
    snprintf(name,sizeof(name),"%.*s - Edited%.32s",(int)length,source,extension?extension:"");
    int rc=prompt_file_text("Save an edited copy on the phone",name,0);
    if(rc)return rc;
    if(!psmtp_name_valid(name))return MTP_STATE;
    if(!psmtp_ui_name_compare(name,source))return MTP_EXISTS;
    text_copy_task task={viewer,name};copy_ui ui={0};
    ui.panel.copy_mode=2;ui.panel.name=name;ui.panel.operation=1;
    app.progress_hidden=0;
    rc=background_call(&ui,save_phone_text_work,&task,viewer->client,operation_progress,1);
    app.phone_dirty=1;
    return rc;
}

static int bounded_change(int current,int delta,int minimum,int maximum){
    current+=delta;
    return current<minimum?minimum:current>maximum?maximum:current;
}

static void inspect_viewer(psmtp_viewer *viewer){
    psmtp_console_selection panel={0};
    char rows[18][256],status[128]="";
    int saved=0;
    panel.view=PSMTP_VIEW_VIEWER;panel.viewer=viewer;panel.text_rows=rows;
    (void)psmtp_screen_buttons();
    for(;;){
        if(viewer->kind==PSMTP_FILE_TEXT){
            panel.text_first=viewer->line/18*18;
            panel.text_count=viewer->lines-panel.text_first;
            if(panel.text_count>18)panel.text_count=18;
            for(size_t i=0;i<panel.text_count;++i){
                size_t line=panel.text_first+i;
                psmtp_viewer_line(viewer,line,line==viewer->line?viewer->column:0,rows[i],sizeof(rows[i]));
            }
        }
        draw_ui(&app.ring,0,status,1,app.scan_status,&panel,NULL);
        if(psmtp_screen_error() || !psmtp_screen_pad_ready() || psmtp_ps4_exit_pending())break;
        uint32_t buttons=read_buttons();
        if(buttons&ORBIS_PAD_BUTTON_CIRCLE){
            if(!viewer->dirty || confirm_action("Discard changes?","Cross discards your edits. Circle returns to the editor."))break;
        }else if(viewer->kind==PSMTP_FILE_IMAGE){
            int zoom=buttons&ORBIS_PAD_BUTTON_R2?25:buttons&ORBIS_PAD_BUTTON_L2?-25:0;
            viewer->zoom=bounded_change(viewer->zoom,zoom,100,400);
            if(buttons&ORBIS_PAD_BUTTON_SQUARE){viewer->zoom=100;viewer->pan_x=viewer->pan_y=0;}
            viewer->pan_x=bounded_change(viewer->pan_x,buttons&ORBIS_PAD_BUTTON_LEFT?-12:buttons&ORBIS_PAD_BUTTON_RIGHT?12:0,-100,100);
            viewer->pan_y=bounded_change(viewer->pan_y,buttons&ORBIS_PAD_BUTTON_UP?-12:buttons&ORBIS_PAD_BUTTON_DOWN?12:0,-100,100);
        }else {
            int rc=0;
            int move=buttons&ORBIS_PAD_BUTTON_L1?-18:buttons&ORBIS_PAD_BUTTON_R1?18:buttons&ORBIS_PAD_BUTTON_UP?-1:buttons&ORBIS_PAD_BUTTON_DOWN?1:0;
            viewer->line=(size_t)bounded_change((int)viewer->line,move,0,(int)viewer->lines-1);
            if(move)viewer->column=0;
            if(buttons&ORBIS_PAD_BUTTON_LEFT)psmtp_viewer_scroll(viewer,-80);
            if(buttons&ORBIS_PAD_BUTTON_RIGHT)psmtp_viewer_scroll(viewer,80);
            if(!viewer->read_only){
                if(buttons&ORBIS_PAD_BUTTON_CROSS)rc=viewer_edit_line(viewer);
                else if(buttons&ORBIS_PAD_BUTTON_SQUARE)rc=psmtp_viewer_edit(viewer,"",1);
                else if(buttons&ORBIS_PAD_BUTTON_TRIANGLE)rc=psmtp_viewer_edit(viewer,"",-1);
                else if((buttons&ORBIS_PAD_BUTTON_OPTIONS) && viewer->dirty){
                    rc=save_viewer_text(viewer);
                    if(!rc){viewer->dirty=0;saved=1;strcpy(status,viewer->client?"Edited copy saved on the phone.":"Saved.");}
                    else if(rc==MTP_VERIFY)strcpy(status,"The file changed on disk. Reopen it before editing.");
                    else if(rc==MTP_EXISTS)strcpy(status,"Choose a different name for the edited copy.");
                    else if(rc==MTP_STATE)strcpy(status,"Enter a valid filename.");
                    else if(rc!=MTP_CANCELLED)strcpy(status,"Save failed. Check the source and destination before retrying.");
                }
                if(rc && rc!=MTP_CANCELLED && !(buttons&ORBIS_PAD_BUTTON_OPTIONS))strcpy(status,rc==MTP_LIMIT?"This line is too long for the keyboard.":"Could not apply the edit.");
                else if(viewer->dirty && buttons && !(buttons&ORBIS_PAD_BUTTON_OPTIONS))status[0]=0;
            }
        }
        sceKernelUsleep(1000);
    }
    if(saved){psmtp_preview_invalidate();refresh_folder();}
}

typedef struct {psmtp_media_player *player;psmtp_viewer *viewer;} media_open_task;
static int media_open_background(void *context,psmtp_work *work){
    media_open_task *task=context;
    psmtp_viewer *v=task->viewer;
    if(v->client){
        int rc=psmtp_stage_file(&v->source,v->client,v->staged_directory,v->staged_path,work);
        if(rc)return rc;
    }
    psmtp_work_progress(work,MTP_COPY_PREPARE,0,0);
    if(psmtp_work_cancelled(work))return MTP_CANCELLED;
    int rc=psmtp_media_open(&task->player,task->viewer);
    return !rc && psmtp_work_cancelled(work)?MTP_CANCELLED:rc;
}

static int media_loading_progress(void *context,enum mtp_copy_phase phase,uint64_t done,uint64_t total){
    copy_ui *ui=context;
    uint32_t buttons=read_buttons();
    uint64_t now=ui_now();
    if(!ui->frames || now>=ui->next_present){
        ++ui->frames;
        ui->next_present=now+50;
        ui->panel.animation_ms=now;
        ui->panel.done=done;
        ui->panel.total=phase==MTP_COPY_RECEIVE?total:0;
        draw_ui(&app.ring,0,ui->cancelling?"Cancelling…":"Please wait",1,app.scan_status,&ui->panel,NULL);
    }
    return psmtp_screen_error() || (buttons&ORBIS_PAD_BUTTON_CIRCLE);
}

static int play_viewer(psmtp_viewer *viewer){
    if(app.media_retiring){
        if(psmtp_media_close_poll(app.media_player,0)){
            strcpy(app.status,"Previous playback is closing. Open the file again in a moment.");
            return MTP_CANCELLED;
        }
        app.media_player=NULL;app.media_retiring=0;
    }
    media_open_task task={.viewer=viewer};copy_ui ui={0};
    psmtp_media_player *player=NULL;
    psmtp_console_selection panel={.view=PSMTP_VIEW_MEDIA,.name=viewer->source.name,.viewer=viewer,.media_loading=1,.media_controls=1};
    ui.panel=panel;
    draw_ui(&app.ring,0,"Please wait",1,app.scan_status,&panel,NULL);
    if(psmtp_audio_close(app.stream))return MTP_TIMEOUT;
    int rc=background_call(&ui,media_open_background,&task,viewer->client,media_loading_progress,1),control_error=0;
    if(!rc && psmtp_work_cancelled(app.work))rc=MTP_CANCELLED;
    player=task.player;
    app.media_player=player;
    uint64_t drawn_sequence=0,next_refresh=0;
    int redraw=1,was_suspended=0;
    while(!rc){
        if(psmtp_ps4_exit_pending() || psmtp_screen_error() || !psmtp_screen_pad_ready())break;
        int system_pause;
        unsigned epoch;
        psmtp_lifecycle_transfer_state(&system_pause,&epoch);
        if(system_pause)psmtp_screen_submit_done();
        if(system_pause!=was_suspended){
            rc=psmtp_media_system_pause(player,system_pause);
            if(rc)break;
            was_suspended=system_pause;
            redraw=1;
        }
        if(system_pause){sceKernelUsleep(10000);continue;}
        psmtp_media_frame frame;
        rc=psmtp_media_update(player,&frame);
        if(rc)break;
        panel.media_pixels=frame.pixels;panel.media_width=frame.width;panel.media_height=frame.height;
        panel.media_time=frame.time;panel.media_duration=frame.duration;panel.media_volume=frame.volume;panel.media_paused=frame.paused || frame.ended;panel.media_aspect=frame.aspect;
        panel.media_loading=frame.loading;
        panel.animation_ms=ui_now();
        const char *status=control_error?"This media does not support that control.":frame.audio_error?"Audio output is unavailable.":
            frame.loading?"Please wait":frame.ended?"Playback ended.":frame.paused?"Paused":"Playing";
        if(redraw || drawn_sequence!=frame.sequence ||
           ((panel.media_controls || !frame.pixels) && panel.animation_ms>=next_refresh)){
            draw_ui(&app.ring,0,status,1,app.scan_status,&panel,NULL);
            drawn_sequence=frame.sequence;
            next_refresh=panel.animation_ms+(frame.loading?40:100);
            redraw=0;
        }else sceKernelUsleep(2000);
        uint32_t buttons=read_buttons();
        if(buttons&ORBIS_PAD_BUTTON_CIRCLE)break;
        if(frame.loading)continue;
        if(buttons)redraw=1;
        if(frame.pixels && (buttons&ORBIS_PAD_BUTTON_OPTIONS))panel.media_controls=!panel.media_controls;
        if(buttons&ORBIS_PAD_BUTTON_CROSS)control_error=psmtp_media_pause(player);
        if(buttons&ORBIS_PAD_BUTTON_LEFT)control_error=psmtp_media_seek(player,-10000);
        if(buttons&ORBIS_PAD_BUTTON_RIGHT)control_error=psmtp_media_seek(player,10000);
        if(buttons&ORBIS_PAD_BUTTON_SQUARE)control_error=psmtp_media_seek(player,0);
        if(buttons&ORBIS_PAD_BUTTON_TRIANGLE)psmtp_media_volume(player,0);
        if(buttons&ORBIS_PAD_BUTTON_UP)psmtp_media_volume(player,5);
        if(buttons&ORBIS_PAD_BUTTON_DOWN)psmtp_media_volume(player,-5);
    }
    psmtp_screen_submit_done();
    app.media_retiring=psmtp_media_close_poll(player,0)!=0;
    psmtp_screen_submit_done();
    if(!app.media_retiring){
        app.media_player=NULL;
        if(!psmtp_ps4_exit_pending())psmtp_audio_open(app.stream);
    }
    return rc;
}

static int open_viewer(const char *name){
    psmtp_viewer viewer={0};
    copy_ui ui={0};
    viewer.kind=psmtp_viewer_kind(name);viewer.zoom=100;
    if(!viewer.kind)return 0;
    if(!app.phone_current && app.local_current && app.local_current->entries[app.local_current->selected].incomplete){
        notice_dialog("Download incomplete","Finish transferring this file before opening it.");return 1;
    }
    app.preview_pause=1;preview_quiet();
    int rc=capture_selection(&viewer.source);
    if(!rc && !viewer.source.folder){
        viewer.client=app.phone_current?app.phone_current->client:NULL;
        app.copying=1;app.progress_hidden=0;ui.panel.copy_mode=2;ui.panel.name=viewer.source.name;
        if(viewer.kind==PSMTP_FILE_VIDEO || viewer.kind==PSMTP_FILE_MUSIC)rc=play_viewer(&viewer);
        else {
            rc=background_call(&ui,psmtp_viewer_load,&viewer,viewer.client,operation_progress,1);
            if(!rc)inspect_viewer(&viewer);
        }
        app.copying=0;
    }
    if(rc==MTP_CANCELLED && viewer.client && viewer.client->poisoned && app.phone_current){
        /* Stream-only phones must reopen after an interrupted GetObject.
         * Only carry names across the reset; a fresh session can assign new handles. */
        mtp_navigator *nav=app.phone_current;
        app.switch_requested=1;
        app.media_return.pending=nav->identity_valid;
        memcpy(app.media_return.identity,nav->device_identity,32);
        app.media_return.storage=nav->storages[nav->storage_index];
        app.media_return.depth=nav->depth;
        for(size_t i=0;i<nav->depth;++i)
            strcpy(app.media_return.names[i],i+1<nav->depth?nav->history[i+1].folder:nav->folder_name);
        strcpy(app.media_return.names[nav->depth],viewer.source.name);
    }
    psmtp_viewer_close(&viewer);app.preview_pause=0;
    if(rc && rc!=MTP_CANCELLED)notice_dialog("Could not open file",rc==MTP_LIMIT?"The file exceeds the viewer's size limit.":
        rc==MTP_UNSUPPORTED?"The format or codec is unavailable on this system.":rc==MTP_PROTOCOL?"The contents are damaged or use an unsupported encoding.":
        rc==MTP_NO_SPACE?"There is not enough space to prepare the file.":"Check the file and source storage, then try again.");
    (void)psmtp_screen_buttons();
    return 1;
}

static int prompt_file_text(const char *title,char input[256],int secret){
    if(wait_for_key_release(ORBIS_PAD_BUTTON_CROSS|ORBIS_PAD_BUTTON_OPTIONS|ORBIS_PAD_BUTTON_TRIANGLE))return MTP_CANCELLED;
    int rc=secret?psmtp_ime_secret(title,input,256,viewer_keyboard_pump,NULL,app.stream):
        psmtp_ime_text(title,input,256,viewer_keyboard_pump,NULL,app.stream);
    if(psmtp_ime_busy())psmtp_ps4_exit(1,app.file);
    (void)psmtp_screen_buttons();
    return rc;
}

static uint32_t archive_picker_input(psmtp_console_selection *panel){
    for(;;){
        draw_ui(&app.ring,0,panel->notice_title,1,app.scan_status,panel,NULL);
        uint32_t buttons=read_buttons();
        if(viewer_keyboard_pump(NULL) || (buttons&(ORBIS_PAD_BUTTON_CIRCLE|ORBIS_PAD_BUTTON_OPTIONS)))return 0;
        if((buttons&ORBIS_PAD_BUTTON_UP) && panel->index)--panel->index;
        if((buttons&ORBIS_PAD_BUTTON_DOWN) && panel->index+1<panel->count)++panel->index;
        if(buttons&(ORBIS_PAD_BUTTON_CROSS|ORBIS_PAD_BUTTON_SQUARE))return buttons;
    }
}

static int archive_choose(const char *title,const char *const *choices,size_t count,size_t selected){
    psmtp_console_selection panel={.view=PSMTP_VIEW_COMPRESS,.notice_title=title,.archive_choices=choices,
        .archive_hint="Cross Select     Circle Back",.count=count,.index=selected};
    uint32_t buttons;
    do buttons=archive_picker_input(&panel);while(buttons && !(buttons&ORBIS_PAD_BUTTON_CROSS));
    return buttons?(int)panel.index:-1;
}

static int archive_pick_folder(psmtp_archive_job *job){
    const char *storage[PSMTP_USB_SLOTS+3];
    psmtp_location locations[PSMTP_USB_SLOTS+2];
    size_t count=1;
    storage[0]="Current source folder";
    if(app.phone_current){locations[count-1]=app.target;storage[count++]="Browse phone folders";}
    for(size_t i=0;i<app.locations.count;++i){
        const psmtp_location *loc=&app.locations.entries[i];
        if(loc->kind==PSMTP_LOCATION_PHONE || count>=sizeof(storage)/sizeof(*storage))continue;
        locations[count-1]=*loc;storage[count++]=loc->label;
    }
    int choice=archive_choose("Output storage",storage,count,0);
    if(choice<0)return MTP_CANCELLED;
    if(!choice){
        job->destination=app.target;
        if(app.phone_current){
            const mtp_navigator *nav=app.phone_current;
            job->phone_parent=nav->parent;
            job->phone_storage=nav->storages[nav->storage_index];
            snprintf(job->directory,sizeof(job->directory),"Phone / %.240s",nav->folder_name);
        }else strcpy(job->directory,app.local_current->path);
        return 0;
    }
    const psmtp_location *location=&locations[choice-1];
    int on_phone=location->kind==PSMTP_LOCATION_PHONE,rc=0;
    mtp_navigator *phone=on_phone?malloc(sizeof(*phone)):NULL;
    psmtp_local_browser *local=on_phone?NULL:calloc(1,sizeof(*local));
    size_t capacity=on_phone?MTP_MAX_ITEMS:PSMTP_LOCAL_ITEMS;
    const char **folders=malloc((capacity+1)*sizeof(*folders));
    size_t *indices=malloc(capacity*sizeof(*indices));
    if(!folders || !indices || (!phone && !local)){rc=MTP_MEMORY;goto finish;}
    if(phone){
        *phone=*app.phone_current;
        phone->catalog=NULL;
        phone->scan_progress=catalog_progress;
        rc=mtp_nav_refresh(phone);
    }else rc=psmtp_local_browse(local,location->root,location->root);
    psmtp_console_selection panel={.view=PSMTP_VIEW_COMPRESS,.notice_title="Output folder",.archive_choices=folders,
        .archive_hint="Cross Select     Square Parent folder     Circle Back"};
    char phone_path[256];
    while(!rc){
        if(phone)snprintf(phone_path,sizeof(phone_path),"Phone / %.240s",phone->folder_name);
        panel.destination=phone?phone_path:local->path;
        panel.count=1;
        folders[0]="Save in this folder";
        size_t entries=phone?phone->count:local->count;
        for(size_t i=0;i<entries;++i){
            const char *name=NULL;
            if(phone && phone->catalog && mtp_is_folder(&phone->catalog[i].info))name=phone->catalog[i].info.name;
            if(local && local->entries[i].folder)name=local->entries[i].name;
            if(name){indices[panel.count-1]=i;folders[panel.count++]=name;}
        }
        uint32_t buttons=archive_picker_input(&panel);
        if(!buttons){rc=MTP_CANCELLED;break;}
        if(buttons&ORBIS_PAD_BUTTON_SQUARE){
            if(phone && phone->depth)rc=mtp_nav_back(phone);
            if(local && strcmp(local->path,local->root))rc=psmtp_local_back(local);
        }else if(panel.index){
            size_t selected=indices[panel.index-1];
            if(phone){
                rc=mtp_nav_move(phone,(int)selected-(int)phone->selected);
                if(!rc)rc=mtp_nav_enter(phone);
            }else {local->selected=selected;rc=psmtp_local_enter(local);}
        }else {
            if(local){
                int fd=psmtp_op_writable_directory(location,local->path);
                if(fd<0){rc=MTP_FILESYSTEM;break;}
                close(fd);
            }
            job->destination=*location;
            strcpy(job->directory,panel.destination);
            if(phone){job->phone_storage=phone->storages[phone->storage_index];job->phone_parent=phone->parent;}
            break;
        }
        panel.index=0;
    }
finish:
    if(phone && folders && indices)free(phone->catalog);
    free(phone);free(local);free(folders);free(indices);
    return rc;
}

static void default_archive_name(psmtp_archive_job *job){
    if(job->count>1){strcpy(job->name,"Archive");return;}
    const char *name=job->sources[0].name,*extension=NULL;
    if(!job->sources[0].folder && !psmtp_archive_single_file(job->format)){
        extension=psmtp_archive_suffix(name);
        if(!extension)extension=strrchr(name,'.');
    }
    size_t length=extension && extension!=name?(size_t)(extension-name):strlen(name);
    if(length>180)length=180;
    while(((unsigned char)name[length]&0xc0)==0x80)--length;
    snprintf(job->name,sizeof(job->name),"%.*s",(int)length,name);
}

static int compression_settings(psmtp_archive_job *job){
    static const char *const levels[]={"0","1","2","3","4","5","6","7","8","9"};
    static const char *const seven_zip_levels[]={"0  Store","1  Fast","2","3","4","5","6  Highest"};
    const char *formats[PSMTP_ARCHIVE_FORMATS];
    enum psmtp_archive_format format_ids[PSMTP_ARCHIVE_FORMATS];
    size_t format_count=0,format_index=0;
    int single=job->count==1 && !job->sources[0].folder,custom_name=0;
    for(int i=0;i<PSMTP_ARCHIVE_FORMATS;++i){
        enum psmtp_archive_format format=(enum psmtp_archive_format)i;
        if(!single && psmtp_archive_single_file(format))continue;
        formats[format_count]=psmtp_archive_format_label(format);
        format_ids[format_count++]=format;
    }
    psmtp_console_selection panel={.view=PSMTP_VIEW_COMPRESS,.name=job->name,.destination=job->directory};
    job->level=1;
    default_archive_name(job);
    for(;;){
        int zip=job->format==PSMTP_ARCHIVE_ZIP;
        const char *const *level_labels=job->format==PSMTP_ARCHIVE_7Z?seven_zip_levels:levels;
        panel.archive_extension=psmtp_archive_extension(job->format);
        panel.archive_level=level_labels[job->level];
        panel.archive_password=job->password[0]?(zip?PSMTP_ARCHIVE_AES_ZIP:PSMTP_ARCHIVE_AES_WRAPPER):
            PSMTP_ARCHIVE_UNPROTECTED;
        draw_ui(&app.ring,0,"Compress",1,app.scan_status,&panel,NULL);
        uint32_t buttons=read_buttons();
        if(viewer_keyboard_pump(NULL) || (buttons&(ORBIS_PAD_BUTTON_CIRCLE|ORBIS_PAD_BUTTON_OPTIONS)))return MTP_CANCELLED;
        if((buttons&ORBIS_PAD_BUTTON_UP) && panel.index)--panel.index;
        if((buttons&ORBIS_PAD_BUTTON_DOWN) && panel.index+1<PSMTP_COMPRESS_ROWS)++panel.index;
        if(!(buttons&ORBIS_PAD_BUTTON_CROSS))continue;
        panel.edit_error=NULL;
        int rc=0;
        switch(panel.index){
        case PSMTP_COMPRESS_FORMAT: {
            int choice=archive_choose("Archive format",formats,format_count,format_index);
            if(choice<0)break;
            if(format_ids[choice]!=job->format)job->level=1;
            format_index=(size_t)choice;
            job->format=format_ids[choice];
            if(!custom_name)default_archive_name(job);
            job->level=(unsigned)bounded_change((int)job->level,0,(int)psmtp_archive_min_level(job->format),(int)psmtp_archive_max_level(job->format));
            break;
        }
        case PSMTP_COMPRESS_LEVEL: {
            unsigned first=psmtp_archive_min_level(job->format),last=psmtp_archive_max_level(job->format);
            int choice=archive_choose("Compression level",level_labels+first,last-first+1,job->level-first);
            if(choice>=0)job->level=first+(unsigned)choice;
            break;
        }
        case PSMTP_COMPRESS_PASSWORD:
            rc=prompt_file_text("Archive password (empty removes it)",job->password,1);
            if(rc && rc!=MTP_CANCELLED)panel.edit_error="Could not open the password keyboard. Try again.";
            break;
        case PSMTP_COMPRESS_NAME:
            rc=prompt_file_text("Archive name without extension",job->name,0);
            if(!rc)custom_name=1;
            if(rc && rc!=MTP_CANCELLED)panel.edit_error="Could not edit the archive name. Try again.";
            break;
        case PSMTP_COMPRESS_DESTINATION:
            rc=archive_pick_folder(job);
            if(rc && rc!=MTP_CANCELLED)panel.edit_error="Could not open that folder. Check storage access.";
            break;
        case PSMTP_COMPRESS_START: {
            char filename[sizeof(job->name)];
            int n=snprintf(filename,sizeof(filename),"%s%s%s",job->name,panel.archive_extension,
                panel.archive_password==PSMTP_ARCHIVE_AES_WRAPPER?".zip":"");
            size_t dir_length=strlen(job->destination.kind==PSMTP_LOCATION_PHONE?
                PSMTP_COPY_DIRECTORY "/.upload-XXXXXXXX":job->directory);
            if(!job->name[0] || n<0 || (size_t)n>=sizeof(filename) || !psmtp_name_valid(filename) ||
               dir_length+1+(size_t)n>=PSMTP_LOCAL_PATH){
                panel.edit_error="Enter a valid, shorter archive name.";
                panel.index=PSMTP_COMPRESS_NAME;
                break;
            }
            strcpy(job->name,filename);
            return 0;
        }
        }
    }
}


static void archive_sources(const psmtp_op_ref *sources,size_t count,int extracting){
    psmtp_archive_job job={.sources=sources,.count=count,.extracting=extracting};
    copy_ui ui={0};
    int rc=0;
    if(!count)return;
    if(extracting && (sources->folder || !psmtp_archive_suffix(sources->name))){
        strcpy(app.status,"Select a supported archive to extract.");return;
    }
    if(app.phone_current){
        mtp_navigator *nav=app.phone_current;
        job.client=nav->client;
        job.destination=app.target;
        job.phone_parent=nav->parent;
        job.phone_storage=nav->storages[nav->storage_index];
        snprintf(job.directory,sizeof(job.directory),"Phone / %.240s",nav->folder_name);
    }else if(app.local_current){job.destination=app.target;strcpy(job.directory,app.local_current->path);}
    else return;
    app.preview_pause=1;preview_quiet();
    if(extracting){
        size_t length=(size_t)(psmtp_archive_suffix(sources->name)-sources->name);
        if(length>180)length=180;
        while(((unsigned char)sources->name[length]&0xc0)==0x80)--length;
        snprintf(job.name,sizeof(job.name),"%.*s - Extracted",(int)length,sources->name);
    }else {
        rc=compression_settings(&job);
    }
    ui.panel.copy_mode=2;ui.panel.name=job.name;ui.panel.operation=1;
    ui.panel.action=extracting?PSMTP_OP_EXTRACT:PSMTP_OP_COMPRESS;
    while(!rc){
        app.copying=1;app.progress_hidden=0;
        rc=background_call(&ui,psmtp_archive_run,&job,job.client,operation_progress,1);
        app.copying=0;
        if(!extracting || (rc!=PSMTP_ARCHIVE_REQUIRED && rc!=PSMTP_ARCHIVE_PASSWORD))break;
        int previous=rc;
        psmtp_archive_forget(job.password);
        rc=prompt_file_text(previous==PSMTP_ARCHIVE_PASSWORD?"Wrong password. Try again":"Archive password",job.password,1);
        if(!rc && !job.password[0])rc=MTP_CANCELLED;
    }
    psmtp_archive_forget(job.password);
    psmtp_unstage_file(job.staged_directory,job.staged_path);
    app.preview_pause=0;
    if(job.destination.kind==PSMTP_LOCATION_PHONE && (!rc || job.uncertain_upload))app.phone_dirty=1;
    if(!rc){
        psmtp_preview_invalidate();
        if(!job.client)reload_local();
        snprintf(app.status,sizeof(app.status),"%s %.96s",extracting?"Extracted to":"Created",job.name);
        if(job.client || strcmp(job.directory,app.local_current->path))
            notice_dialog(extracting?"Archive extracted":"Archive created",job.directory);
    }else if(job.uncertain_upload)notice_dialog(rc==MTP_CANCELLED?"Archive operation cancelled":"Archive operation failed",
        extracting?"Check the archive's folder on the phone for partial extracted files before retrying.":
        "Check the phone for a partial archive before retrying.");
    else if(rc==MTP_CANCELLED)strcpy(app.status,"Archive operation cancelled.");
    else notice_dialog("Archive operation failed",
        rc==PSMTP_ARCHIVE_ENCRYPTION?"This archive's encryption method is unsupported.":
        rc==MTP_EXISTS?"The output already exists or the archive contains duplicate paths.":
        rc==MTP_NO_SPACE?"There is not enough free storage.":
        rc==MTP_LIMIT?"The selection exceeds the archive's size or path limits.":
        rc==MTP_PROTOCOL?"The archive is damaged or contains unsafe entries.":
        rc==MTP_STATE?"Enter a valid archive name.":
        rc==MTP_UNSUPPORTED && job.destination.kind==PSMTP_LOCATION_PHONE?
            (extracting?"The phone's current storage does not support verified uploads.":"The phone does not support verified uploads. Choose PS4 storage or a USB drive."):
        "Check this format and the destination storage, then try again.");
    (void)psmtp_screen_buttons();
}

static void archive_selection(int extracting){
    psmtp_op_ref source;
    preview_quiet();
    if(!capture_selection(&source))archive_sources(&source,1,extracting);
}
#else
static int open_viewer(const char *name){(void)name;return 0;}
static void archive_selection(int extracting){(void)extracting;}
static void archive_sources(const psmtp_op_ref *sources,size_t count,int extracting){(void)sources;(void)count;(void)extracting;}
#endif

typedef struct {
    const psmtp_op_ref *sources;
    size_t count,completed,copied,removed;
    mtp_client *client;
    const psmtp_location *destination;
    const char *path;
    psmtp_native_stat expected_destination;
    uint32_t storage,parent;
    int action,check_destination,uncertain;
} batch_work;

static int batch_work_run(void *context,psmtp_work *work){
    batch_work *batch=context;
    psmtp_op_plan *plan=calloc(1,sizeof(*plan));
    int rc=0;
    if(!plan)return MTP_MEMORY;
    for(size_t index=0;index<batch->count;++index){
        if(psmtp_work_cancelled(work)){rc=MTP_CANCELLED;break;}
        rc=batch->action==2?
            psmtp_op_plan_delete(plan,&batch->sources[index],batch->client,psmtp_work_progress,work,psmtp_work_log(work)):
            psmtp_op_plan_build(plan,&batch->sources[index],batch->client,psmtp_work_progress,work,psmtp_work_log(work));
        if(rc)break;
        if(batch->action==2){
            if(batch->client && !plan->delete_supported){rc=MTP_UNSUPPORTED;break;}
            rc=psmtp_op_delete(plan,batch->client,0,psmtp_work_progress,work);
        }else {
            rc=batch->destination->kind==PSMTP_LOCATION_PHONE?
                psmtp_op_target_phone(plan,batch->client,batch->destination,batch->storage,batch->parent):
                psmtp_op_target(plan,batch->destination,batch->path);
            if(!rc && batch->check_destination && (plan->destination_stat.st_dev!=batch->expected_destination.st_dev ||
                plan->destination_stat.st_ino!=batch->expected_destination.st_ino))rc=MTP_VERIFY;
            if(!rc)rc=psmtp_op_space(plan,batch->client);
            if(!rc)rc=psmtp_op_execute(plan,batch->client,batch->action==1,psmtp_work_progress,work);
        }
        batch->copied+=plan->copied;batch->removed+=plan->removed;
        batch->uncertain=plan->uncertain_upload || plan->uncertain_delete || plan->cleanup_failed;
        if(rc)break;
        ++batch->completed;
    }
    free(plan);
    return rc;
}

static void run_batch(const psmtp_op_ref *sources,size_t count,mtp_client *client,int action,const psmtp_location *destination,const char *path){
    batch_work batch={.sources=sources,.count=count,.client=client,.action=action,.destination=destination,.path=path};
    copy_ui ui={0};
    char title[64],message[160],label[48];
    snprintf(title,sizeof(title),"%s %zu selected items?",action==2?"Delete":action==1?"Move":"Copy",count);
    snprintf(message,sizeof(message),"%s",action==2?"This permanently deletes the selected files and folders.\nCircle returns to the selection.":
        action==1?"Items are removed from the source after their copies verify.":"Existing items at the destination are preserved.");
    app.preview_pause=1;preview_quiet();
    if(!confirm_action(title,message)){app.preview_pause=0;return;}
    int rc=0;
    if(destination && destination->kind==PSMTP_LOCATION_PHONE && app.phone_current){
        batch.storage=app.phone_current->storages[app.phone_current->storage_index];batch.parent=app.phone_current->parent;
    }else if(destination){
        batch.check_destination=1;
        if(app.paste_pending)batch.expected_destination=app.paste_stat;
        else {
            int fd=psmtp_native_directory(destination->root,path);
            if(fd<0)rc=MTP_FILESYSTEM;
            else {rc=psmtp_native_status(fd,&batch.expected_destination);if(close(fd) && !rc)rc=MTP_FILESYSTEM;}
        }
    }
    if(!rc){
        snprintf(label,sizeof(label),"%zu selected items",count);
        ui.panel.copy_mode=2;ui.panel.name=label;ui.panel.operation=1;ui.panel.action=action;ui.panel.item_count=count;
        app.copying=1;app.progress_hidden=0;
        (void)psmtp_screen_buttons();
        transfer_notice(action,0,0);
        rc=background_call(&ui,batch_work_run,&batch,client,operation_progress,1);
        transfer_notice(action,rc,1);
        app.copying=0;
    }
    app.preview_pause=0;
    if(action==1 && batch.completed){
        app.batch_count-=batch.completed;
        if(app.batch_count){
            memmove(app.clipboard_batch,app.clipboard_batch+batch.completed,app.batch_count*sizeof(*app.clipboard_batch));
            app.clipboard=app.clipboard_batch[0];
        }else app.clipboard_active=0;
    }
    if(action==2 && (batch.completed || batch.removed || batch.uncertain)){
        if(app.multi_mode && batch.completed==count)multi_end();
        else if(app.multi_mode && batch.completed){
            app.marked_count-=batch.completed;
            memmove(app.marked,app.marked+batch.completed,app.marked_count*sizeof(*app.marked));
        }
        app.clipboard_active=0;app.batch_count=0;
    }
    if(client && app.retained_phone && client==app.retained_phone->client &&
       (batch.removed || batch.uncertain || (destination && destination->kind==PSMTP_LOCATION_PHONE)))app.phone_dirty=1;
    psmtp_preview_invalidate();
    if(!rc)snprintf(app.status,sizeof(app.status),"%s %zu items.",action==2?"Deleted":action==1?"Moved":"Copied",count);
    else snprintf(app.status,sizeof(app.status),"%zu of %zu finished. %s",batch.completed,count,
        batch.uncertain?"Check both locations before retrying.":rc==MTP_EXISTS?"A destination name already exists.":
        rc==MTP_CANCELLED?"Batch cancelled.":rc==MTP_NO_SPACE?"Storage is full.":"Check the remaining items and storage access.");
    (void)psmtp_screen_buttons();
}

static void batch_action(size_t action){
    if(action==PSMTP_BATCH_COPY || action==PSMTP_BATCH_MOVE){
        app.batch_count=app.marked_count;
        memcpy(app.clipboard_batch,app.marked,app.batch_count*sizeof(*app.marked));
        app.clipboard=app.clipboard_batch[0];app.clipboard_active=1;app.clipboard_cut=action==PSMTP_BATCH_MOVE;
        multi_end();
        snprintf(app.status,sizeof(app.status),"%zu items ready to paste. Open the destination and select Paste.",app.batch_count);
    }else if(action==PSMTP_BATCH_DELETE){
        run_batch(app.marked,app.marked_count,app.phone_current?app.phone_current->client:NULL,2,NULL,NULL);
        if(app.phone_current && !app.phone_current->client->poisoned){app.phone_dirty=0;(void)mtp_nav_refresh(app.phone_current);}
        else reload_local();
    }else if(action==PSMTP_BATCH_COMPRESS)archive_sources(app.marked,app.marked_count,0);
}

static void paste_clipboard(mtp_client *client,const psmtp_location *destination,const char *path){
    int action=app.clipboard_cut?1:0;
    if(app.batch_count)run_batch(app.clipboard_batch,app.batch_count,client,action,destination,path);
    else run_operation(&app.clipboard,client,action,destination,path);
}

static void file_action(size_t action) {
    psmtp_op_ref source;int rc;
    if(action==4) {app.clipboard_active=0;app.batch_count=0;strcpy(app.status,"Clipboard cleared.");return;}
    if(action==0 || action==1) {
        rc=capture_selection(&source);if(rc) {snprintf(app.status,sizeof(app.status),"Could not select this item (%d).",rc);return;}
        app.clipboard=source;app.clipboard_active=1;app.clipboard_cut=action==1;app.batch_count=0;
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
        paste_clipboard(app.phone_current->client,&app.target,NULL);
        if(!app.phone_current->client->poisoned){app.phone_dirty=0;(void)mtp_nav_refresh(app.phone_current);}return;
    }
    if(app.view!=PSMTP_VIEW_LOCAL){strcpy(app.status,"Open a destination folder first.");return;}
    if(!app.local_current || app.local_current->error) {strcpy(app.status,"Open a readable destination folder first.");return;}
    if(app.clipboard.location.kind==PSMTP_LOCATION_PHONE && app.retained_phone && !app.retained_phone->client->poisoned && psmtp_location_same(&app.clipboard.location,&app.retained_location)) {
        paste_clipboard(app.retained_phone->client,&app.target,app.local_current->path);reload_local();
    } else if(app.clipboard.location.kind==PSMTP_LOCATION_PHONE) {
        int fd=psmtp_native_directory(app.target.root,app.local_current->path);if(fd<0) {strcpy(app.status,"Destination is unavailable.");return;}
        rc=psmtp_native_status(fd,&app.paste_stat);if(close(fd)) rc=MTP_FILESYSTEM;if(rc) {strcpy(app.status,"Could not check the destination.");return;}
        app.paste_location=app.target;strcpy(app.paste_path,app.local_current->path);app.paste_pending=1;
        app.target=app.clipboard.location;app.switch_requested=1;
    } else {paste_clipboard(NULL,&app.target,app.local_current->path);reload_local();}
}
static int browse(void *unused,mtp_client *client,FILE *out) {
    mtp_navigator *nav=malloc(sizeof(*nav));int rc=0,closed;uint32_t buttons;psmtp_location session_location=app.target;(void)unused;
    if(!nav) return MTP_MEMORY;
    app.view=PSMTP_VIEW_PHONE;
    rc=mtp_nav_open_ex(nav,client,out,app.paste_pending?NULL:catalog_progress,NULL);if(rc) goto done;
    if(app.sort_mode)(void)mtp_nav_sort(nav,app.sort_mode);app.toolbar_focus=0;
#ifndef PSMTP_TEST_EXPLORER
    if(app.media_return.pending){
        app.media_return.pending=0;
        if(nav->identity_valid && !memcmp(nav->device_identity,app.media_return.identity,32)){
            size_t storage=0;
            while(storage<nav->storage_count && nav->storages[storage]!=app.media_return.storage)++storage;
            if(storage<nav->storage_count){
                if(storage!=nav->storage_index && (rc=mtp_nav_storage(nav,storage)))goto done;
                for(size_t level=0;nav->catalog && level<=app.media_return.depth;++level){
                    int folder=level<app.media_return.depth;
                    size_t i=0;
                    while(i<nav->count && (mtp_is_folder(&nav->catalog[i].info)!=folder ||
                          strcmp(nav->catalog[i].info.name,app.media_return.names[level])))++i;
                    if(i==nav->count)break;
                    if((rc=mtp_nav_move(nav,(int)i-(int)nav->selected)))goto done;
                    if(folder && (rc=mtp_nav_enter(nav)))goto done;
                }
            }
        }
    }
#endif
    app.phone_current=app.retained_phone=nav;app.retained_location=session_location;app.phone_detached=app.phone_dirty=0;
    if(app.paste_pending) {
        paste_clipboard(client,&app.paste_location,app.paste_path);
        app.target=app.paste_location;strcpy(app.resume_path,app.paste_path);app.paste_pending=0;app.switch_requested=1;goto done;
    }
    if(app.locations.count) {snprintf(app.locations.entries[app.locations.active].label,sizeof(app.locations.entries[0].label),"%.95s",nav->device_name);app.locations.entries[app.locations.active].named=2;}
    strcpy(app.status,"X opens folders or reviews files. Options has Copy and Delete. Left switches devices.");copy_checkpoint();
    (void)psmtp_screen_buttons();
    for(;;) {
        const mtp_nav_entry *entry;int handled;
        if(psmtp_ps4_exit_pending()){app.exit_requested=1;break;}
        if(psmtp_lifecycle_background()){psmtp_screen_submit_done();sceKernelUsleep(10000);continue;}
        if(accessory_owns_usb())break;
        (void)poll_devices(0);
        if(app.phone_detached || (!psmtp_preview_phone_busy() && client->poisoned)) {rc=MTP_DISCONNECTED;break;}
        if(app.refresh_requested) {app.refresh_requested=0;(void)poll_devices(1);}
#ifndef PSMTP_TEST_EXPLORER
        if(usb_progress_pending())usb_job_dialog();
        if(app.exit_requested)break;
#endif
        if(app.target.kind!=PSMTP_LOCATION_PHONE) {
            psmtp_console_selection panel={0};app.phone_current=NULL;
            if(app.switch_requested) {app.switch_requested=0;activate_local(app.local_current);}
            idle_refresh();panel.view=PSMTP_VIEW_LOCAL;panel.local=app.local_current;
            draw_ui(&app.ring,0,app.status,1,app.scan_status,&panel,NULL);
            if(psmtp_screen_error() || !psmtp_screen_pad_ready()) {rc=MTP_IO;break;}
            buttons=file_buttons();if(!shared_controls(buttons)) local_step(app.local_current,buttons);
            if(usb_target_missing()) internal_fallback(app.local_current,"USB drive disconnected. Switched to internal storage.");
            if(app.exit_requested || accessory_owns_usb()) break;
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
        if(app.exit_requested || accessory_owns_usb()) break;
        if(app.refresh_requested || app.switch_requested) continue;
        if(handled) {sceKernelUsleep(1000);continue;}
        if(!psmtp_preview_phone_busy() && client->poisoned) {rc=MTP_IO;break;}
        entry=mtp_nav_selected(nav);
        if((buttons&ORBIS_PAD_BUTTON_CROSS) && (!entry || !mtp_is_folder(&entry->info))) {
            if(entry) {
                if(psmtp_pkg_name(entry->info.name)){rc=install_selection();if(client->poisoned)break;rc=0;continue;}
                if(!open_viewer(entry->info.name))properties();rc=0;
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
    if(app.exit_requested){
        psmtp_ps4_usb_stop();
        psmtp_work_shutdown(app.usb_receiver);
        psmtp_audio_stop();
        if(psmtp_preview_close(out))psmtp_ps4_exit(1,app.file);
        client->timeout_ms=250;
    }
    else preview_quiet();
    app.phone_current=app.retained_phone=NULL;app.phone_detached=app.phone_dirty=0;closed=mtp_nav_close(nav);free(nav);return rc?rc:closed;
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
    if(app.exit_requested)return;
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
        else if(!open_viewer(local->entries[local->selected].name))properties();
    }
}
static void internal_fallback(psmtp_local_browser *local,const char *message) {
    size_t i;for(i=0;i<app.locations.count;++i) if(app.locations.entries[i].kind==PSMTP_LOCATION_INTERNAL) {
        app.target=app.locations.entries[i];activate_local(local);break;
    }
    if(message) snprintf(app.status,sizeof(app.status),"%s",message);
}
#if !defined(PSMTP_TEST_EXPLORER) || defined(PSMTP_TEST_WORK_CLEANUP)
/* All process exit routes converge here. Polling runs on the UI thread, so
 * disabling dispatch stops its timers without another thread to join. */
static int shutdown_close_file(FILE **file){
    FILE *f=*file;int first=0,fd;*file=NULL;if(!f || f==stdout || f==stderr)return 0;
    if((fflush)(f))first=MTP_FILESYSTEM;fd=fileno(f);
    if(fd>=0 && fsync(fd) && !first)first=MTP_FILESYSTEM;
    if(fclose(f) && !first)first=MTP_FILESYSTEM;return first;
}
static int app_shutdown_step(void *unused,enum psmtp_shutdown_step step,psmtp_shutdown *s){
    FILE *quiet=stderr;int rc=0,code;unsigned borrowers=s->retained&(PSMTP_RETAIN_WORK|PSMTP_RETAIN_PREVIEW);(void)unused;
    if(!borrowers && (s->retained&PSMTP_RETAIN_INSTALL) && psmtp_install_phone_borrowed())borrowers=1;
    switch(step){
    case PSMTP_SHUTDOWN_STOP:
        app.shutting_down=app.exit_requested=1;app.monitor_live=0;app.refresh_requested=app.switch_requested=0;app.screen_ready=0;
        psmtp_lifecycle_shutdown_begin();psmtp_ps4_usb_stop();psmtp_install_stop();psmtp_work_shutdown(app.work);psmtp_work_shutdown(app.usb_receiver);psmtp_preview_stop();psmtp_audio_stop();
        psmtp_lifecycle_mark("shutdown-stop-published",0);break;
    case PSMTP_SHUTDOWN_DRAIN:
        psmtp_screen_submit_done();
#ifndef PSMTP_TEST_EXPLORER
        if(app.media_player){
            code=psmtp_media_close(app.media_player);
            psmtp_lifecycle_mark("media-drain-returned",code);
            if(code){s->retained|=PSMTP_RETAIN_AUDIO;rc=code;}
            else app.media_player=NULL;
        }
#endif
        code=psmtp_work_join(app.work,5000);psmtp_lifecycle_mark("operation-drain-returned",code);if(code){s->retained|=PSMTP_RETAIN_WORK;rc=code;}
        if(app.usb_receiver){code=psmtp_work_join(app.usb_receiver,2000);psmtp_lifecycle_mark("usb-download-drain-returned",code);if(code){s->retained|=PSMTP_RETAIN_WORK;if(!rc)rc=code;}}
#ifndef PSMTP_TEST_EXPLORER
        if(!(s->retained&PSMTP_RETAIN_WORK))psmtp_usb_download_release(&app.usb_receiver_report);
#endif
        code=psmtp_preview_close(quiet);psmtp_lifecycle_mark("preview-drain-returned",code);if(code){s->retained|=PSMTP_RETAIN_PREVIEW;if(!rc)rc=code;}
        code=psmtp_audio_close(quiet);psmtp_lifecycle_mark("audio-drain-returned",code);if(code){s->retained|=PSMTP_RETAIN_AUDIO;if(!rc)rc=code;}
        code=psmtp_ime_shutdown(quiet);psmtp_lifecycle_mark("ime-drain-returned",code);if(code){s->retained|=PSMTP_RETAIN_IME;if(!rc)rc=code;}
#ifndef PSMTP_TEST_EXPLORER
        code=psmtp_system_dialog_shutdown(quiet);if(code){s->retained|=PSMTP_RETAIN_IME;if(!rc)rc=code;}
#endif
        if(!(s->retained&PSMTP_RETAIN_WORK)){code=psmtp_install_shutdown(quiet);psmtp_lifecycle_mark("installer-term-returned",code);if(code && !rc)rc=code;if(psmtp_install_retained())s->retained|=PSMTP_RETAIN_INSTALL;}
        else s->retained|=PSMTP_RETAIN_INSTALL;
        break;
    case PSMTP_SHUTDOWN_FILES:
        /* Worker-owned FDs are disposed by their jobs after cancellation. A
         * stranded job retains those FDs, its mailbox and borrowed context. */
        if(!borrowers){if(app.retained_phone){free(app.retained_phone->catalog);free(app.retained_phone);}app.retained_phone=app.phone_current=NULL;free(app.local_current);app.local_current=NULL;free(app.update_job);app.update_job=NULL;}
        if(!(s->retained&(PSMTP_RETAIN_WORK|PSMTP_RETAIN_INSTALL)) && app.work){code=psmtp_work_destroy(app.work);if(code)rc=code;else app.work=NULL;}
        if(!(s->retained&PSMTP_RETAIN_WORK) && app.usb_receiver){code=psmtp_work_destroy(app.usb_receiver);if(code && !rc)rc=code;if(!code)app.usb_receiver=NULL;}
        diag_log_set_sink(&app.ring,NULL,NULL);
        if(app.stream && app.stream!=app.file && app.stream!=app.scan_file){FILE *cookie=app.stream;app.stream=NULL;if(cookie!=stdout && cookie!=stderr && fclose(cookie))rc=MTP_FILESYSTEM;}else app.stream=NULL;
        code=shutdown_close_file(&app.scan_file);if(code && !rc)rc=code;code=shutdown_close_file(&app.file);if(code && !rc)rc=code;
        psmtp_lifecycle_mark("shutdown-files-closed",rc);break;
    case PSMTP_SHUTDOWN_USB:
        if(borrowers){s->retained|=PSMTP_RETAIN_USB;rc=MTP_STATE;}
        else {rc=psmtp_ps4_usb_shutdown_report(quiet);if(rc)s->retained|=PSMTP_RETAIN_USB;}
        psmtp_lifecycle_mark("usb-shutdown-returned",rc);break;
    case PSMTP_SHUTDOWN_ACCESS:
        if(s->retained&(PSMTP_RETAIN_WORK|PSMTP_RETAIN_PREVIEW|PSMTP_RETAIN_INSTALL|PSMTP_RETAIN_USB)){s->retained|=PSMTP_RETAIN_ACCESS;rc=MTP_STATE;}
        else {rc=psmtp_process_access_restore(quiet);if(rc)s->retained|=PSMTP_RETAIN_ACCESS;}
        psmtp_lifecycle_mark("filesystem-release-returned",rc);break;
    case PSMTP_SHUTDOWN_PRESENTATION:
        rc=psmtp_screen_shutdown(quiet,!(s->retained&PSMTP_RETAIN_IME));if(rc)s->retained|=PSMTP_RETAIN_DISPLAY;
        psmtp_lifecycle_mark("screen-close-returned",rc);psmtp_lifecycle_mark("shutdown-resources-retained",(int)s->retained);break;
    default:return MTP_STATE;
    }return rc;
}
static FILE *app_shutdown(void){
    int rc=psmtp_shutdown_run(&app.shutdown,app_shutdown_step,NULL);
    if(__atomic_load_n(&app.shutdown.state,__ATOMIC_ACQUIRE)==2)psmtp_lifecycle_mark("shutdown-sequence-finished",rc);
    return NULL;
}

#endif
#ifndef PSMTP_TEST_EXPLORER
int main(void) {
    uint32_t buttons;int rc,access_rc;
#ifndef PSMTP_PRODUCTION
    cookie_io_functions_t sink={0};int log_errno;
#endif
    psmtp_local_browser *local;
    psmtp_ps4_exit_set_cleanup(app_shutdown);
    local=calloc(1,sizeof(*local));app.local_current=local;
    diag_log_init(&app.ring);app.names=1;app.initial_launch=1;app.refresh_requested=1;
    app.view=PSMTP_VIEW_HOME;strcpy(app.status,"Finding connected devices...");strcpy(app.scan_status,"Logs ready");
#ifndef PSMTP_PRODUCTION
    app.file=open_log(PSMTP_DIAGNOSTIC_LOG_PATH,"a");log_errno=errno;if(app.file) setvbuf(app.file,NULL,_IOFBF,65536);
    sink.write=log_write;app.stream=fopencookie(NULL,"w",sink);if(!app.stream) psmtp_ps4_exit(1,app.file);setvbuf(app.stream,NULL,_IONBF,0);
    fputs("\n=== PS MTP Explorer " PSMTP_VERSION " ===\n",app.stream);
    if(!app.file) fprintf(app.stream,"General log unavailable errno=%d\n",log_errno);
#else
    app.stream=psmtp_release_log_stream();
#endif
    /* Complete filesystem credentials before graphics/controller modules start
     * their helper threads. Logs are live before the first HEN probe. */
    scan_log_begin();fputs("=== BEGIN DEVICE EXPLORER " PSMTP_VERSION " names=0 ===\n",app.stream);
    fprintf(app.stream,"stage=process-access uid=%u euid=%u in-sandbox=%d\n",(unsigned)getuid(),(unsigned)geteuid(),sceKernelIsInSandbox());
    (void)psmtp_lifecycle_open(app.stream);
    psmtp_lifecycle_mark("process-access-enter",0);access_rc=psmtp_process_access(app.stream);psmtp_lifecycle_mark("process-access-returned",access_rc);
    if(!access_rc)(void)psmtp_lifecycle_open(app.stream);
    {struct timespec check={0};if(psmtp_monotonic_time(&check)){
        fputs("stage=clock-unavailable native-monotonic-id=4; stopping safely\n",app.stream);goto shutdown;}}
#ifndef PSMTP_PRODUCTION
    clock_diagnostic();
#endif
    if(!access_rc) {uint64_t available,block;int space_rc=psmtp_native_space("/","/data",&available,&block,app.stream);
        fprintf(app.stream,"stage=storage-startup status=%d available=%" PRIu64 "\n",space_rc,available);}
    if(!access_rc)load_preferences();
    copy_checkpoint();
    psmtp_preview_open(app.stream);
    rc=psmtp_screen_open(app.stream);
    if(rc || !local) psmtp_ps4_exit(1,app.file);
    app.local_current=local;local->scan_progress=catalog_progress;local->scan_user=local;
    app.screen_ready=1;
    if(access_rc!=0) {
#ifdef PSMTP_PRODUCTION
        notice_dialog("Storage unavailable","Storage access could not be initialized. Close the app, enable GoldHEN and try again.");
#else
        diagnostics();
#endif
        goto shutdown;
    }
    check_for_updates(0);
    if(psmtp_ps4_exit_pending() || app.exit_requested)goto shutdown;
    psmtp_audio_open(app.stream);
    rc=psmtp_local_prepare_copies(app.stream);
    if(rc) fprintf(app.stream,"stage=storage-ready status=%d errno=%d; copy container preparation failed, check storage-prepare/output-access\n",rc,errno);
    copy_checkpoint();
    app.usb_mask=psmtp_local_usb_scan(app.stream);psmtp_locations_build(&app.locations,NULL,0,app.usb_mask);
    while(!app.exit_requested && !psmtp_ps4_exit_pending()) {
        if(psmtp_lifecycle_background()){psmtp_screen_submit_done();sceKernelUsleep(10000);continue;}
        psmtp_console_selection panel={0};
        if(accessory_owns_usb()){
            if(app.refresh_requested || !app.monitor_live){
                app.refresh_requested=app.initial_launch=0;
                app.monitor_live=1;
                (void)poll_devices(1);
            }
            if(app.target.kind==PSMTP_LOCATION_PHONE || !app.target.root[0]){
                app.switch_requested=0;
                internal_fallback(local,NULL);
            }
        }
        if(app.refresh_requested || (app.switch_requested && app.target.kind==PSMTP_LOCATION_PHONE)) {
            psmtp_ps4_options options={0};int connecting=app.initial_launch || (app.switch_requested && app.target.kind==PSMTP_LOCATION_PHONE);
            app.refresh_requested=app.switch_requested=0;
            options.communicate=connecting;options.names=1;options.choose_device=choose;options.devices=devices;options.browse=browse;
            panel.view=PSMTP_VIEW_HOME;strcpy(app.status,connecting?"Opening the connected phone...":"Refreshing connected devices...");
            if(app.initial_launch) draw_ui(&app.ring,0,app.status,1,app.scan_status,&panel,NULL);
            if(!psmtp_screen_pad_ready()) {app.exit_requested=1;break;}
            rc=psmtp_ps4_diagnose(&options,app.stream);
#ifndef PSMTP_TEST_EXPLORER
            if(!app.switch_requested)app.media_return.pending=0;
#endif
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
        if(usb_progress_pending())usb_job_dialog();
        if(app.exit_requested || psmtp_screen_error())break;
        idle_refresh();panel.view=PSMTP_VIEW_LOCAL;panel.local=local;
        draw_ui(&app.ring,0,app.status,1,app.scan_status,&panel,NULL);
        if(psmtp_screen_error()) break;
        buttons=file_buttons();if(!shared_controls(buttons)) local_step(local,buttons);
        if(poll_devices(0)) internal_fallback(local,"USB drive unavailable. Switched to internal storage.");
        sceKernelUsleep(1000);
    }
shutdown:
    psmtp_ps4_exit(psmtp_screen_error() || access_rc!=0?1:0,app.file);
}
#endif
