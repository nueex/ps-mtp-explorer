/*
 * PS MTP Explorer
 * Original Author: nueex
 * Copyright (C) 2026 nueex
 * SPDX-License-Identifier: GPL-3.0-or-later
 * GNU GPL version 3 or later; see LICENSE and LICENSING.md.
 * WITHOUT ANY WARRANTY; see LICENSE.
 * Historical MIT permissions/notices: LICENSES/MIT-legacy.txt.
 */
#ifndef PSMTP_DEVICE_AUDIO_H
#define PSMTP_DEVICE_AUDIO_H
#include <stdio.h>
void psmtp_audio_open(FILE *);
void psmtp_audio_notify(unsigned,FILE *);
void psmtp_audio_status(FILE *);
void psmtp_audio_close(FILE *);
#endif
