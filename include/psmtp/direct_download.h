/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PSMTP_DIRECT_DOWNLOAD_H
#define PSMTP_DIRECT_DOWNLOAD_H

#include "psmtp/log_paths.h"
#include <stddef.h>
#include <stdint.h>

#define PSMTP_DOWNLOAD_DIRECTORY PSMTP_LOG_DIRECTORY "/Downloads"
#define PSMTP_DOWNLOAD_DISPLAY "/psmtp/Downloads/"
#define PSMTP_DOWNLOAD_PART_NAME ".psmtp-direct-download.part"
#define PSMTP_DOWNLOAD_NAME_MAX 200u
#define PSMTP_DOWNLOAD_CHUNK (128u * 1024u)
#define PSMTP_DOWNLOAD_MAX (1024ULL * 1024u * 1024u * 1024u)
#define PSMTP_INSTALL_WAIT_REPLY "PSR1\0\0\0\6"

/* USB and LAN protocol, one job at a time. Big-endian fields.
 * Phone: "PSD1" (save) or "PSI1" (install), u32 name length, u64 size
 * (UINT64_MAX if unknown, save only), name.
 * PS4: "PSR1", u32 result. Zero accepts the offer.
 * Phone: repeated u32 length + data (up to CHUNK), each with a PS4 result.
 * Phone: u32 zero + SHA-256 to finish.
 * PS4: "PSR1", u32 final result. Zero means saved or installed as requested.
 * An install may receive interim status 6 while awaiting native completion.
 * USB sends the offer header, name, each length, body and digest separately.
 * A PSQ1 header with zero fields queries the destination volume. The PSS1
 * response carries a 32-bit status and 64-bit free and total byte counts.
 * A PSD2/PSI2 offer appends a 32-byte job/representation key and the highest
 * usable offset on the phone. A successful PSR1 is followed by the saved
 * 64-bit offset. Frame UINT32_MAX plus the current connection's SHA-256
 * commits and acknowledges a pause after syncing the received bytes. An
 * already verified prefix is retained while the rest of the file is received.
 * UINT32_MAX-1 stops the active transfer, including during an install wait.
 * PSX1 (save) or PSX2 (install) plus name, size and a job key discards a
 * previously paused transfer. Status 8 confirms that cleanup finished. */
#define PSMTP_DOWNLOAD_STOP_FRAME (UINT32_MAX - 1u)
#define PSMTP_DOWNLOAD_PARTS_FRAME (UINT32_MAX - 2u)
#define PSMTP_DOWNLOAD_PARTS_MAX 8u
#define PSMTP_DOWNLOAD_PARTS_ALPN "psmtp-parts/1"
/* Accessory version 2 and the multipart ALPN support this optional frame:
 * u32 marker, u8 count (2/4/8), u64 source bytes/sec, u8 percent[8], PSR1. */
#define PSMTP_DOWNLOAD_STOPPED_LOCAL 1 /* Internal successful stop, distinct from MTP_CANCELLED. */
enum psmtp_download_result {
    PSMTP_DOWNLOAD_ACCEPTED,
    PSMTP_DOWNLOAD_REJECTED,
    PSMTP_DOWNLOAD_FULL,
    PSMTP_DOWNLOAD_EXISTS,
    PSMTP_DOWNLOAD_INVALID_DATA,
    PSMTP_DOWNLOAD_WRITE_ERROR,
    PSMTP_DOWNLOAD_INSTALL_WAIT,
    PSMTP_DOWNLOAD_INSTALL_ERROR,
    PSMTP_DOWNLOAD_STOPPED,
    PSMTP_DOWNLOAD_SOURCE_CONNECT,
    PSMTP_DOWNLOAD_SOURCE_TIMEOUT,
    PSMTP_DOWNLOAD_SOURCE_RESPONSE,
    PSMTP_DOWNLOAD_SOURCE_PKG,
    PSMTP_DOWNLOAD_SOURCE_BUSY,
    PSMTP_DOWNLOAD_SOURCE_UNAVAILABLE,
    PSMTP_DOWNLOAD_INSTALL_BASE_MISSING,
    PSMTP_DOWNLOAD_INSTALL_START,
    PSMTP_DOWNLOAD_NO_INTERNET,
    PSMTP_DOWNLOAD_HTTP_STATUS = 0x10000
};

/* PSL1 (save) and PSL2 (parallel install) use the same 16-byte header.
 * Wait for PSR1 before sending the name, u32 payload length and four
 * NUL-terminated fields: final HTTPS URL, User-Agent, Referer, Cookie.
 * The second PSR1 confirms the console opened the source and destination.
 * From then on no reads or writes on the phone connection are required. */
#define PSMTP_LINK_MAX 16384u
typedef struct {
    const char *url, *agent, *referer, *cookie;
} psmtp_download_link;

typedef struct {
    void *user;
    int (*read)(void *, void *, size_t, int);
    int (*reply)(void *, const void *, size_t);
    int (*begin)(void *, const char *, uint64_t);
    int (*write)(void *, const void *, size_t, uint64_t, uint64_t);
    int (*publish)(void *, uint64_t, const uint8_t[32]);
    int (*begin_install)(void *, const char *, uint64_t);
    int (*capacity)(void *, uint64_t *, uint64_t *);
    int (*begin_resume)(void *, const char *, uint64_t, int, const uint8_t[32], uint64_t, uint64_t *);
    int (*checkpoint)(void *, uint64_t);
    int (*stop)(void *);
    int (*discard)(void *, const char *, uint64_t, int, const uint8_t[32]);
    int (*open_link)(void *, const char *, uint64_t, int, const psmtp_download_link *);
    int (*run_link)(void *);
    uint32_t (*error_status)(void *, int);
    void (*parts)(void *, unsigned, uint64_t, const uint8_t[PSMTP_DOWNLOAD_PARTS_MAX]);
} psmtp_download_receiver;

int psmtp_download_receive(const psmtp_download_receiver *);
#endif
