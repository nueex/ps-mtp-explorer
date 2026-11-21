/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#ifndef PSMTP_CONSOLE_RENDER_H
#define PSMTP_CONSOLE_RENDER_H
#include "psmtp/diag_log.h"
#include "psmtp/navigator.h"
#include "file_store.h"
#include "psmtp/locations.h"
#include "psmtp/metadata.h"
#include "psmtp/transfer_meter.h"
#define PSMTP_SCREEN_WIDTH 1920
#define PSMTP_SCREEN_HEIGHT 1080
typedef struct {const char *name,*modified;uint64_t bytes;size_t items;int folder,unknown,items_known,thumbnail;} psmtp_ui_item;
struct psmtp_viewer;

typedef struct {
    size_t index,count;
    uint16_t vendor_id,product_id;
    int copy_mode,phase;
    uint32_t handle;
    uint64_t done,total,source_total;
    const char *name;
    int view,folder;
    size_t files,folders,item_index,item_count;
    const psmtp_local_browser *local;
    const psmtp_locations *locations;
    int drawer_focus;
    const char *clipboard_label,*destination;
    int operation,action,can_select,can_paste,can_extract;
    int multi_mode;
    size_t marked_count;
    int (*marked)(size_t);
    int speed_boost,preferences_unsaved,install_mode,direct_download,settings_context,settings_index,theme;uint64_t animation_ms;
    int theme_choice;
    int usb_receiver_ready, usb_receive_phase, usb_download, usb_install;
    int wireless;
    const char *lan_address;
    const unsigned char *lan_qr;
    int lan_code, lan_decision;
    uint64_t usb_receive_done, usb_receive_total;
    unsigned usb_part_count;
    uint64_t usb_part_percentages, usb_source_speed;
    psmtp_transfer_meter usb_receive_meter;
    int list_view,drawer_settings,menu_kind,size_unknown,transfer_active;
    const mtp_navigator *browser;
    const char *modified,*clock_text,*display_version,*path;
    const psmtp_ui_item *display_items;size_t display_count,display_selected;
    const psmtp_ui_item *detail_override;uint64_t elapsed_ms;
    /* Nonzero only during one serialized job. A new job gets a new epoch. */
    uint64_t busy_epoch;
    uint64_t metadata_revision;int metadata_revision_known;
    psmtp_transfer_meter transfer;
    const psmtp_metadata *(*metadata)(void *,size_t);void *metadata_user;
    const char *notice_title,*notice_text;
    int toolbar_focus,sort_mode,rename_allowed,can_create,install_percent,install_known,install_preparing;const char *edit_name,*edit_error;int keyboard_shift;
    const struct psmtp_viewer *viewer;
    const char (*text_rows)[256];
    size_t text_first,text_count;
    const uint32_t *media_pixels;
    unsigned media_width,media_height,media_volume;
    float media_aspect;
    uint64_t media_time,media_duration;
    int media_paused,media_controls,media_loading;
    const char *archive_extension,*archive_level;
    int archive_password;
    const char *const *archive_choices;
    const char *archive_hint;
} psmtp_console_selection;
enum { PSMTP_MENU_OPEN,PSMTP_MENU_NEW_FOLDER,PSMTP_MENU_COPY,PSMTP_MENU_MOVE,PSMTP_MENU_PASTE,PSMTP_MENU_RENAME,PSMTP_MENU_DELETE,PSMTP_MENU_PROPERTIES,PSMTP_MENU_EXTRACT,PSMTP_MENU_COMPRESS,PSMTP_MENU_EXIT,PSMTP_MENU_COUNT };
enum { PSMTP_BATCH_COPY,PSMTP_BATCH_MOVE,PSMTP_BATCH_DELETE,PSMTP_BATCH_COMPRESS,PSMTP_BATCH_COUNT };
enum { PSMTP_SETTINGS_SPEED, PSMTP_SETTINGS_INSTALL, PSMTP_SETTINGS_THEME, PSMTP_SETTINGS_DOWNLOAD, PSMTP_SETTINGS_DOWNLOAD_MODE, PSMTP_SETTINGS_UPDATE, PSMTP_SETTINGS_RESET, PSMTP_SETTINGS_COUNT };
enum { PSMTP_VIEW_HOME,PSMTP_VIEW_PHONE,PSMTP_VIEW_LOCAL,PSMTP_VIEW_TRANSFERS,PSMTP_VIEW_LOGS,PSMTP_VIEW_PICK,PSMTP_VIEW_INFO,PSMTP_VIEW_MENU,PSMTP_VIEW_LOADING,PSMTP_VIEW_OPERATION,PSMTP_VIEW_SETTINGS,PSMTP_VIEW_CONTROLS,PSMTP_VIEW_RENAME,PSMTP_VIEW_SORT,PSMTP_VIEW_NOTICE,PSMTP_VIEW_INSTALL_CONFIRM,PSMTP_VIEW_INSTALL_PROGRESS,PSMTP_VIEW_INSTALL_MODE,PSMTP_VIEW_UPDATE,PSMTP_VIEW_VIEWER,PSMTP_VIEW_MEDIA,PSMTP_VIEW_CONFIRM,PSMTP_VIEW_COMPRESS };
void psmtp_console_render(uint32_t *, const diag_log *, size_t, const char *, int, const char *, const psmtp_console_selection *,const mtp_navigator *);
int psmtp_console_render_cached(void);
void psmtp_console_render_invalidate(void);
void psmtp_console_render_close(void);
/* Per display buffer, initialize generation to zero. Copy only changed regions
 * after that buffer has received the current complete static scene. */
size_t psmtp_console_render_upload(uint32_t *,const uint32_t *,uint64_t *);
#endif
