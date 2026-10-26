/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#ifndef PSMTP_UI_DRAW_H
#define PSMTP_UI_DRAW_H
#include <stdint.h>
#define UI_RGB(r,g,b) (0xff000000u|(uint32_t)(r)|((uint32_t)(g)<<8)|((uint32_t)(b)<<16))
#define UI_WHITE UI_RGB(237,231,232)
#define UI_MUTED UI_RGB(155,148,150)
#define UI_LINE UI_RGB(53,50,54)
#define UI_PANEL UI_RGB(29,27,28)
#define UI_GOLD UI_RGB(244,173,55)
enum {UI_WORDMARK,UI_PLATFORM,UI_FOLDER,UI_PHONE,UI_USB,UI_DRIVE,UI_SETTINGS,UI_HOME,UI_COPY,UI_MOVE,UI_RENAME,UI_DELETE,UI_INFO,UI_DOCUMENT,UI_THUMB_PKG,UI_THUMB_IMAGE,UI_THUMB_VIDEO};
enum {UI_BTN_CROSS,UI_BTN_CIRCLE,UI_BTN_TRIANGLE,UI_BTN_SQUARE,UI_BTN_DPAD,UI_BTN_L1,UI_BTN_R1,UI_BTN_L2,UI_BTN_R2,UI_BTN_L3,UI_BTN_R3,UI_BTN_TOUCH,UI_BTN_OPTIONS};
void ui_button(int,int,int,int,uint32_t);
void ui_draw_begin(uint32_t *);
void ui_draw_close(void);
void ui_rect(int,int,int,int,uint32_t);
void ui_alpha(int,int,int,int,uint32_t,unsigned);
void ui_box(int,int,int,int,int,uint32_t,uint32_t,int);
void ui_line(int,int,int,int,int,uint32_t,unsigned);
void ui_ring(int,int,int,int,uint32_t,unsigned);
void ui_art(int,int,int,int,int,uint32_t);
void ui_text(int,int,int,int,const char *,uint32_t);
void ui_center(int,int,int,int,const char *,uint32_t);
void ui_shadow(int,int,int,int,int);
void ui_backdrop(void);
void ui_image(const uint32_t *,unsigned,unsigned,int,int,int,int);
#endif
