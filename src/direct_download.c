/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#include "psmtp/direct_download.h"
#include "psmtp/mtp.h"
#include "psmtp/name.h"
#include "psmtp/sha256.h"
#include <stdlib.h>
#include <string.h>

static uint32_t read_u32(const uint8_t *bytes) {
    return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 |
           (uint32_t)bytes[2] << 8 | bytes[3];
}

static void write_u64(uint8_t *bytes, uint64_t value) {
    for (int i = 7; i >= 0; --i, value >>= 8) bytes[i] = (uint8_t)value;
}

static int reply(const psmtp_download_receiver *receiver, int status) {
    uint8_t bytes[8] = {'P', 'S', 'R', '1', 0, 0, 0, PSMTP_DOWNLOAD_WRITE_ERROR};
    switch (status) {
        case MTP_OK: bytes[7] = PSMTP_DOWNLOAD_ACCEPTED; break;
        case PSMTP_DOWNLOAD_STOPPED_LOCAL: bytes[7] = PSMTP_DOWNLOAD_STOPPED; break;
        case MTP_NO_SPACE: bytes[7] = PSMTP_DOWNLOAD_FULL; break;
        case MTP_EXISTS: bytes[7] = PSMTP_DOWNLOAD_EXISTS; break;
        case MTP_PROTOCOL:
        case MTP_LIMIT:
        case MTP_VERIFY: bytes[7] = PSMTP_DOWNLOAD_INVALID_DATA; break;
        case MTP_CANCELLED: bytes[7] = PSMTP_DOWNLOAD_REJECTED; break;
        case MTP_UNSUPPORTED: bytes[7] = PSMTP_DOWNLOAD_INSTALL_ERROR; break;
        default: break;
    }
    uint32_t detail = status < 0 && receiver->error_status ? receiver->error_status(receiver->user, status) : 0;
    if (detail) for (unsigned i = 0; i < 4; ++i) bytes[7 - i] = (uint8_t)(detail >> (i * 8));
    return receiver->reply(receiver->user, bytes, sizeof(bytes));
}

static int receive_link(const psmtp_download_receiver *receiver, char *name, uint64_t size, int install) {
    uint8_t length[4];
    void *user = receiver->user;
    int rc = receiver->read(user, length, sizeof(length), 0);
    if (rc) return rc;
    uint32_t count = read_u32(length);
    char *payload = NULL;
    if (count < 4 || count > PSMTP_LINK_MAX) rc = MTP_LIMIT;
    else if (!(payload = malloc(count))) rc = MTP_MEMORY;
    else rc = receiver->read(user, payload, count, 0);
    if (!rc) {
        char *at = payload, *end = payload + count;
        const char *fields[4];
        for (unsigned i = 0; i < 4; ++i) {
            char *next = memchr(at, 0, (size_t)(end - at));
            if (!next) { rc = MTP_PROTOCOL; break; }
            for (char *p = at; p < next; ++p)
                if ((unsigned char)*p < 32 || *p == 127) { rc = MTP_PROTOCOL; break; }
            if (rc) break;
            fields[i] = at;
            at = next + 1;
        }
        if (!rc && (at != end || strncmp(fields[0], "https://", 8))) rc = MTP_PROTOCOL;
        if (!rc) {
            psmtp_download_link link = {fields[0], fields[1], fields[2], fields[3]};
            rc = receiver->open_link(user, name, size, install, &link);
        }
    }
    free(payload);
    int sent = reply(receiver, rc);
    if (rc || sent) return rc ? rc : sent;
    return receiver->run_link(user);
}

int psmtp_download_receive(const psmtp_download_receiver *receiver) {
    char name[PSMTP_DOWNLOAD_NAME_MAX + 1];
    uint8_t header[16], expected[32], digest[32];
    uint8_t *buffer = NULL;
    uint32_t name_length, length;
    uint64_t size, done = 0;
    void *user = receiver->user;
    psmtp_sha256 hash;
    int rc = receiver->read(user, header, sizeof(header), 1);
    if (rc) return rc;
    if (!memcmp(header, "PSQ1", 4) && !read_u32(header + 4) && !read_u32(header + 8) && !read_u32(header + 12)) {
        uint8_t response[24] = {'P', 'S', 'S', '1'};
        uint64_t available = 0, total = 0;
        response[7] = !receiver->capacity || receiver->capacity(user, &available, &total);
        write_u64(response + 8, available);
        write_u64(response + 16, total);
        return receiver->reply(user, response, sizeof(response));
    }
    name_length = read_u32(header + 4);
    size = (uint64_t)read_u32(header + 8) << 32 | read_u32(header + 12);
    int link = !memcmp(header, "PSL1", 4) || !memcmp(header, "PSL2", 4);
    int resume = !memcmp(header, "PSI2", 4) || !memcmp(header, "PSD2", 4);
    int install = !memcmp(header, "PSI1", 4) || !memcmp(header, "PSI2", 4);
    int discard = !memcmp(header, "PSX1", 4) || !memcmp(header, "PSX2", 4);
    if ((!link && !resume && !install && !discard && memcmp(header, "PSD1", 4)) || !name_length || name_length > PSMTP_DOWNLOAD_NAME_MAX ||
        (size != UINT64_MAX && size > PSMTP_DOWNLOAD_MAX)) {
        (void)reply(receiver, MTP_PROTOCOL);
        return MTP_PROTOCOL;
    }
    if (install && (!receiver->begin_install || size == UINT64_MAX || !size)) {
        (void)reply(receiver, MTP_UNSUPPORTED);
        return MTP_UNSUPPORTED;
    }
    if (resume && (!receiver->begin_resume || !receiver->checkpoint || size == UINT64_MAX)) {
        (void)reply(receiver, MTP_UNSUPPORTED);
        return MTP_UNSUPPORTED;
    }
    if (link) {
        rc = receiver->open_link && receiver->run_link ? MTP_OK : MTP_UNSUPPORTED;
        int sent = reply(receiver, rc);
        if (rc || sent) return rc ? rc : sent;
    }
    rc = receiver->read(user, name, name_length, 0);
    if (rc) return rc;
    name[name_length] = '\0';
    if (name[0] == '.' || memchr(name, 0, name_length) || !psmtp_name_valid(name)) {
        (void)reply(receiver, MTP_PROTOCOL);
        return MTP_PROTOCOL;
    }
    if (link) return receive_link(receiver, name, size, header[3] == '2');
    if (discard) {
        if (!receiver->discard || size == UINT64_MAX) rc = MTP_UNSUPPORTED;
        else {
            rc = receiver->read(user, expected, sizeof(expected), 0);
            if (rc) return rc;
            rc = receiver->discard(user, name, size, header[3] == '2', expected);
        }
        int sent = reply(receiver, rc ? rc : PSMTP_DOWNLOAD_STOPPED_LOCAL);
        return rc ? rc : sent ? sent : MTP_CANCELLED;
    }
    if (resume) {
        rc = receiver->read(user, expected, sizeof(expected), 0);
        if (!rc) rc = receiver->read(user, header, 8, 0);
        if (rc) return rc;
        uint64_t maximum = (uint64_t)read_u32(header) << 32 | read_u32(header + 4);
        rc = maximum <= size ? receiver->begin_resume(user, name, size, install, expected, maximum, &done) : MTP_LIMIT;
    } else rc = install ? receiver->begin_install(user, name, size) : receiver->begin(user, name, size);
    if (!rc) buffer = malloc(PSMTP_DOWNLOAD_CHUNK);
    if (!rc && !buffer) rc = MTP_MEMORY;
    int sent = reply(receiver, rc);
    if (rc || sent) {
        free(buffer);
        return rc ? rc : sent;
    }
    if (resume) {
        write_u64(header, done);
        rc = receiver->reply(user, header, 8);
        if (rc) { free(buffer); return rc; }
    }

    psmtp_sha256_init(&hash);
    for (;;) {
        rc = receiver->read(user, header, 4, 0);
        if (rc) break;
        length = read_u32(header);
        if (length == PSMTP_DOWNLOAD_STOP_FRAME) {
            rc = receiver->stop ? receiver->stop(user) : MTP_UNSUPPORTED;
            sent = reply(receiver, rc);
            if (rc == PSMTP_DOWNLOAD_STOPPED_LOCAL) rc = sent ? sent : MTP_CANCELLED;
            else if (!rc) rc = sent;
            break;
        }
        if (length == PSMTP_DOWNLOAD_PARTS_FRAME && receiver->parts) {
            uint8_t data[1 + 8 + PSMTP_DOWNLOAD_PARTS_MAX];
            rc = receiver->read(user, data, sizeof(data), 0);
            if (rc) break;
            unsigned parts = data[0];
            uint64_t speed = (uint64_t)read_u32(data + 1) << 32 | read_u32(data + 5);
            if ((parts != 2 && parts != 4 && parts != 8) || size == UINT64_MAX ||
                speed > PSMTP_DOWNLOAD_MAX) rc = MTP_PROTOCOL;
            for (unsigned i = 0; !rc && i < parts; ++i)
                if (data[9 + i] > 100) rc = MTP_PROTOCOL;
            if (!rc) receiver->parts(user, parts, speed, data + 9);
            sent = reply(receiver, rc);
            if (rc || sent) { if (!rc) rc = sent; break; }
            continue;
        }
        if (!length || (resume && length == UINT32_MAX)) {
            rc = receiver->read(user, expected, sizeof(expected), 0);
            if (rc) break;
            psmtp_sha256_final(&hash, digest);
            if ((!length && size != UINT64_MAX && size != done) || memcmp(expected, digest, sizeof(digest)))
                rc = MTP_VERIFY;
            else if (length)
                rc = receiver->checkpoint(user, done);
            else
                rc = receiver->publish(user, done, digest);
            sent = reply(receiver, rc);
            if (rc == PSMTP_DOWNLOAD_STOPPED_LOCAL) rc = sent ? sent : MTP_CANCELLED;
            else if (!rc && sent) rc = sent;
            else if (!rc && length) rc = MTP_CANCELLED;
            break;
        }
        if (length > PSMTP_DOWNLOAD_CHUNK || length > PSMTP_DOWNLOAD_MAX - done ||
            (size != UINT64_MAX && length > size - done)) {
            rc = MTP_LIMIT;
            (void)reply(receiver, rc);
            break;
        }
        rc = receiver->read(user, buffer, length, 0);
        if (rc) break;
        psmtp_sha256_update(&hash, buffer, length);
        done += length;
        rc = receiver->write(user, buffer, length, done, size);
        /* Acknowledging each chunk gives a blocked phone a bounded amount of
         * outstanding data and propagates disk errors before it sends more. */
        sent = reply(receiver, rc);
        if (rc || sent) {
            if (!rc) rc = sent;
            break;
        }
    }
    free(buffer);
    return rc;
}
