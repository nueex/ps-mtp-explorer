/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PSMTP_USB_DOWNLOAD_H
#define PSMTP_USB_DOWNLOAD_H
#include "work.h"
#include "psmtp/lan_pair.h"
#include "psmtp/install_error.h"
typedef struct psmtp_usb_install psmtp_usb_install;
typedef struct {
    const char *setup_stage, *file_stage;
    int32_t native_status;
    int file_error, offered, ready;
    uint32_t source_error;
    unsigned part_count;
    uint64_t part_percentages, source_speed;
    int installing, paused;
    int wireless;
    char lan_address[16];
    unsigned char lan_qr[PSMTP_LAN_QR_BYTES];
    psmtp_lan_pair_ui pairing;
    psmtp_usb_install *install;
} psmtp_usb_download_report;
int psmtp_usb_download_run(void *, psmtp_work *);
psmtp_usb_install *psmtp_usb_install_acquire(psmtp_usb_download_report *);
void psmtp_usb_install_release(void *);
void psmtp_usb_download_release(psmtp_usb_download_report *);
void psmtp_usb_install_finish(psmtp_usb_install *, int, int, const psmtp_install_failure *);
uint64_t psmtp_usb_install_received(const psmtp_usb_install *);
const char *psmtp_usb_install_name(const psmtp_usb_install *);
/* Returns 1 while the header is arriving, 0 when readable, or an MTP error. */
int psmtp_usb_install_source(psmtp_usb_install *, psmtp_phone_source *);
void psmtp_usb_install_retain(void *);
#endif
