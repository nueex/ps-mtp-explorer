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
    int operation,action,can_select,can_paste;
    int list_view,drawer_settings,menu_kind,size_unknown,transfer_active;
    const mtp_navigator *browser;
    const char *modified,*clock_text,*display_version,*path;
    const psmtp_ui_item *display_items;size_t display_count,display_selected;
    const psmtp_ui_item *detail_override;uint64_t elapsed_ms;
    psmtp_transfer_meter transfer;
    const psmtp_metadata *(*metadata)(void *,size_t);void *metadata_user;
    const char *notice_title,*notice_text;
    int toolbar_focus,sort_mode,rename_allowed,can_create,install_percent,install_known,install_preparing;const char *edit_name,*edit_error;int keyboard_shift;
} psmtp_console_selection;
enum { PSMTP_MENU_OPEN,PSMTP_MENU_NEW_FOLDER,PSMTP_MENU_COPY,PSMTP_MENU_MOVE,PSMTP_MENU_PASTE,PSMTP_MENU_RENAME,PSMTP_MENU_DELETE,PSMTP_MENU_PROPERTIES,PSMTP_MENU_EXIT,PSMTP_MENU_COUNT };
enum { PSMTP_VIEW_HOME,PSMTP_VIEW_PHONE,PSMTP_VIEW_LOCAL,PSMTP_VIEW_TRANSFERS,PSMTP_VIEW_LOGS,PSMTP_VIEW_PICK,PSMTP_VIEW_INFO,PSMTP_VIEW_MENU,PSMTP_VIEW_LOADING,PSMTP_VIEW_OPERATION,PSMTP_VIEW_SETTINGS,PSMTP_VIEW_CONTROLS,PSMTP_VIEW_RENAME,PSMTP_VIEW_SORT,PSMTP_VIEW_NOTICE,PSMTP_VIEW_INSTALL_CONFIRM,PSMTP_VIEW_INSTALL_PROGRESS };
void psmtp_console_render(uint32_t *, const diag_log *, size_t, const char *, int, const char *, const psmtp_console_selection *,const mtp_navigator *);
#endif
