/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PSMTP_PREFERENCES_H
#define PSMTP_PREFERENCES_H
#include <stdio.h>
#define PSMTP_PREFERENCES_DIRECTORY "/data/psmtp"
#define PSMTP_PREFERENCES_PATH PSMTP_PREFERENCES_DIRECTORY "/preferences.cfg"
#define PSMTP_PREFERENCES_TEMP PSMTP_PREFERENCES_DIRECTORY "/preferences.tmp"
enum {PSMTP_INSTALL_FAST=0,PSMTP_INSTALL_PHONE_DIRECT=1};
enum {PSMTP_THEME_LIGHT=0,PSMTP_THEME_DARK,PSMTP_THEME_CLASSIC,PSMTP_THEME_COUNT};
typedef struct {int list_view,sort_mode,speed_boost,install_mode,theme;} psmtp_preferences;
void psmtp_preferences_defaults(psmtp_preferences *);
/* Missing file is normal. Invalid/unreadable data leaves all defaults intact. */
int psmtp_preferences_load(psmtp_preferences *,FILE *);
/* Synchronous UI-owner write: fsync file, atomic rename, fsync directory.
 * No descriptors survive the call; no exit handler is needed for persistence. */
int psmtp_preferences_save(const psmtp_preferences *,FILE *);
#endif
