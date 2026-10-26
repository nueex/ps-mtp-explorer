/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#ifndef PSMTP_KEYBOARD_H
#define PSMTP_KEYBOARD_H
#include <stddef.h>
enum {PSMTP_KEY_CHARACTER,PSMTP_KEY_BACKSPACE,PSMTP_KEY_SHIFT,PSMTP_KEY_CLEAR,PSMTP_KEY_SAVE};
#define PSMTP_KEY_COUNT 44u
int psmtp_key_action(size_t);
char psmtp_key_character(size_t,int);
size_t psmtp_key_move(size_t,int,int);
void psmtp_key_rect(size_t,int *,int *,int *,int *);
#endif
