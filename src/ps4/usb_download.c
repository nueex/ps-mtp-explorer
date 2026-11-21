/* Copyright (C) 2026 nueex. SPDX-License-Identifier: GPL-3.0-or-later */
#define _GNU_SOURCE
#include "usb_download.h"
#include "android_accessory.h"
#include "file_store.h"
#include "monotonic.h"
#include "storage_space.h"
#include "update_client.h"
#include "psmtp/device_watch.h"
#include "psmtp/direct_download.h"
#include "psmtp/sha256.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#ifndef PSMTP_TEST_USB_DOWNLOAD
#include <orbis/libkernel.h>
#include "lan_server.h"
#endif
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    psmtp_android_accessory link;
    psmtp_work *work;
    psmtp_usb_download_report *report;
    int directory, file, published, temporary_created, install_only;
    psmtp_usb_install *install;
#ifndef PSMTP_TEST_USB_DOWNLOAD
    psmtp_lan_server *lan;
#endif
    uint64_t received, accepted_ms;
    int capacity_query;
    uint32_t source_error;
    psmtp_https_stream *source;
    uint64_t link_size;
    uint64_t durable;
    char part[sizeof(PSMTP_DOWNLOAD_DIRECTORY) + 80];
    char checkpoint[sizeof(PSMTP_DOWNLOAD_DIRECTORY) + 80];
    char final[sizeof(PSMTP_DOWNLOAD_DIRECTORY) + PSMTP_DOWNLOAD_NAME_MAX + 1];
} usb_download;

struct psmtp_usb_install {
    int refs, fd, transfer_error, verified, claimed, finished, result, cleanup;
    uint64_t size, received;
    uint32_t wire_error;
    char name[PSMTP_DOWNLOAD_NAME_MAX + 1];
};

static const char download_part[] = PSMTP_DOWNLOAD_DIRECTORY "/" PSMTP_DOWNLOAD_PART_NAME;
#define PSMTP_USBD_TIMEOUT UINT32_C(0x80240007)

void psmtp_usb_install_retain(void *user) {
    psmtp_usb_install *install = user;
    __atomic_add_fetch(&install->refs, 1, __ATOMIC_RELAXED);
}

void psmtp_usb_install_release(void *user) {
    psmtp_usb_install *install = user;
    if (install && !__atomic_sub_fetch(&install->refs, 1, __ATOMIC_ACQ_REL)) {
        (void)close(install->fd);
        free(install);
    }
}

psmtp_usb_install *psmtp_usb_install_acquire(psmtp_usb_download_report *report) {
    psmtp_usb_install *install = __atomic_load_n(&report->install, __ATOMIC_ACQUIRE);
    int unclaimed = 0;
    if (!install || !__atomic_compare_exchange_n(&install->claimed, &unclaimed, 1, 0,
        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return NULL;
    psmtp_usb_install_retain(install);
    return install;
}

void psmtp_usb_download_release(psmtp_usb_download_report *report) {
    psmtp_usb_install_release(__atomic_exchange_n(&report->install, NULL, __ATOMIC_ACQ_REL));
}

void psmtp_usb_install_finish(psmtp_usb_install *install, int status, int cleanup, const psmtp_install_failure *failure) {
    if (status && failure) {
        uint32_t detail = failure->base_missing ? PSMTP_DOWNLOAD_INSTALL_BASE_MISSING :
            failure->native_error >= UINT32_C(0x80000000) ? failure->native_error : PSMTP_DOWNLOAD_INSTALL_START;
        __atomic_store_n(&install->wire_error, detail, __ATOMIC_RELEASE);
    }
    if (status) __atomic_store_n(&install->transfer_error, status, __ATOMIC_RELEASE);
    install->result = status;
    install->cleanup = cleanup;
    __atomic_store_n(&install->finished, 1, __ATOMIC_RELEASE);
}

uint64_t psmtp_usb_install_received(const psmtp_usb_install *install) {
    return __atomic_load_n(&install->received, __ATOMIC_ACQUIRE);
}

const char *psmtp_usb_install_name(const psmtp_usb_install *install) {
    return install->name;
}

static int install_validate(void *user) {
    psmtp_usb_install *install = user;
    return __atomic_load_n(&install->transfer_error, __ATOMIC_ACQUIRE);
}

static uint64_t install_available(void *user) {
    psmtp_usb_install *install = user;
    if (__atomic_load_n(&install->verified, __ATOMIC_ACQUIRE)) return install->size;
    uint64_t received = psmtp_usb_install_received(install);
    /* BGFT can consume the prefix, but a full read requires the USB digest. */
    return received < install->size ? received : install->size - 1;
}

static int install_control(void *user, int *pause, unsigned *epoch) {
    *pause = 0;
    *epoch = 0;
    return install_validate(user);
}

static int install_read(void *user, uint64_t offset, uint8_t *bytes, size_t length) {
    psmtp_usb_install *install = user;
    uint64_t available = install_available(install);
    int rc = install_validate(user);
    if (rc) return rc;
    if (offset > available || length > available - offset) return MTP_STATE;
    if (lseek(install->fd, (off_t)offset, SEEK_SET) != (off_t)offset) return MTP_FILESYSTEM;
    while (length) {
        ssize_t count = read(install->fd, bytes, length);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return MTP_FILESYSTEM;
        bytes += count;
        length -= (size_t)count;
    }
    return MTP_OK;
}

int psmtp_usb_install_source(psmtp_usb_install *install, psmtp_phone_source *source) {
    int rc = install_validate(install);
    if (rc) return rc;
    if (install_available(install) < 0x440) return 1; /* Header still arriving. */
    *source = (psmtp_phone_source){
        .user = install, .size = install->size, .read = install_read, .validate = install_validate,
        .control = install_control, .control_user = install, .available = install_available
    };
    return MTP_OK;
}

static int cancelled(void *user) {
    return psmtp_work_cancelled(((usb_download *)user)->work);
}

static int now_ms(uint64_t *now) {
    struct timespec clock;
    if (psmtp_monotonic_time(&clock)) return MTP_IO;
    *now = (uint64_t)clock.tv_sec * 1000 + (uint64_t)clock.tv_nsec / 1000000;
    return MTP_OK;
}

static int read_usb(void *user, void *buffer, size_t length, int first) {
    usb_download *job = user;
#ifndef PSMTP_TEST_USB_DOWNLOAD
    if(job->lan)return psmtp_lan_read(job->lan,buffer,length);
#endif
    uint8_t *bytes = buffer;
    uint64_t started, now;
    uint64_t last_check;
    size_t offset = 0;
    int rc = now_ms(&started);
    if (rc) return rc;
    last_check = started;
    while (offset < length) {
        size_t got;
        if (cancelled(job)) return MTP_CANCELLED;
        rc = psmtp_accessory_transfer(&job->link, 0, bytes + offset, length - offset, &got, 750);
        if (rc && rc != MTP_IO) return rc;
        if (rc && (uint32_t)job->link.native_status != PSMTP_USBD_TIMEOUT)
            return MTP_DISCONNECTED;
        offset += got;
        if (offset == length) return MTP_OK;
        if (now_ms(&now)) return MTP_IO;
        if (got) started = now;
        if (first && !offset) {
            if (now - last_check >= 2000u) {
                int presence = psmtp_accessory_present(&job->link);
                if (presence == MTP_DISCONNECTED) return presence;
                last_check = now;
            }
        } else if (now - started >= 20000u) return MTP_TIMEOUT;
        if (rc) sceKernelUsleep(20000);
    }
    return MTP_OK;
}

static int reply_usb(void *user, const void *bytes, size_t length) {
    usb_download *job = user;
#ifndef PSMTP_TEST_USB_DOWNLOAD
    if(job->lan)return psmtp_lan_write(job->lan,bytes,length);
#endif
    size_t sent;
    int rc = psmtp_accessory_transfer(&job->link, 1, (void *)(uintptr_t)bytes, length, &sent, 750);
    return rc ? rc : sent == length ? MTP_OK : MTP_IO;
}

static int file_error(void) {
    return errno == ENOSPC ? MTP_NO_SPACE : MTP_FILESYSTEM;
}

static int save_failure(usb_download *job, const char *stage, int status, int error) {
    if (job->report) {
        job->report->file_stage = stage;
        job->report->file_error = error;
    }
    fprintf(psmtp_work_log(job->work), "stage=usb-download-save operation=%s status=%d errno=%d bytes=%" PRIu64 "\n",
        stage, status, error, job->received);
    return status;
}

static int open_file(void *user, const char *name, uint64_t size, uint64_t maximum) {
    usb_download *job = user;
    FILE *log = psmtp_work_log(job->work);
    int existing, rc;
    if (job->report) job->report->offered = 1;
    job->directory = psmtp_native_directory("/", PSMTP_DOWNLOAD_DIRECTORY);
    if (job->directory < 0 && errno == ENOENT) {
        int parent = psmtp_native_directory("/", PSMTP_LOG_DIRECTORY);
        if (parent < 0) return file_error();
        rc = mkdir(PSMTP_DOWNLOAD_DIRECTORY, 0755);
        if (rc && errno != EEXIST) {
            rc = file_error();
            (void)close(parent);
            return rc;
        }
        if (close(parent)) return file_error();
        job->directory = psmtp_native_directory("/", PSMTP_DOWNLOAD_DIRECTORY);
    }
    if (job->directory < 0) return file_error();
    if (job->checkpoint[0]) {
        int marker = open(job->checkpoint, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
        if (marker >= 0) {
            psmtp_native_stat state;
            ssize_t count = read(marker, &job->durable, sizeof(job->durable));
            int invalid = psmtp_native_status(marker, &state) || !S_ISREG(state.st_mode) ||
                state.st_size != sizeof(job->durable) || count != sizeof(job->durable) || job->durable > size;
            if (close(marker) || invalid) return MTP_VERIFY;
        } else if (errno != ENOENT) return file_error();
    }
    if (job->durable > maximum) return MTP_VERIFY;
    job->received = job->durable;
    if (!job->durable && unlink(job->part) && errno != ENOENT) return file_error();
    if (size != UINT64_MAX) {
        rc = psmtp_space_require("/", PSMTP_DOWNLOAD_DIRECTORY, size - job->durable, log);
        if (rc) return rc;
    }
    snprintf(job->final, sizeof(job->final), "%s/%s", PSMTP_DOWNLOAD_DIRECTORY, name);
    if (!job->install_only) {
        existing = open(job->final, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
        if (existing >= 0) {
            (void)close(existing);
            return MTP_EXISTS;
        }
        if (errno != ENOENT)
            return save_failure(job, "checking destination filename", errno == ELOOP ? MTP_EXISTS : file_error(), errno);
    }
    int flags = job->durable ? O_WRONLY : O_WRONLY | O_CREAT | O_EXCL;
    job->file = open(job->part, flags | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC, 0600);
    if (job->file >= 0 && job->durable) {
        psmtp_native_stat state;
        if (psmtp_native_status(job->file, &state) || !S_ISREG(state.st_mode) ||
            state.st_size < 0 || (uint64_t)state.st_size < job->durable || (uint64_t)state.st_size > size ||
            lseek(job->file, (off_t)job->received, SEEK_SET) != (off_t)job->received)
            return save_failure(job, "opening saved checkpoint", MTP_VERIFY, 0);
    }
    if (job->file < 0) return save_failure(job, "creating temporary file", file_error(), errno);
    job->temporary_created = 1;
    if (job->install_only) {
        psmtp_native_stat writer, reader;
        int fd = open(job->part, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) return save_failure(job, "opening temporary file for installation", file_error(), errno);
        if (psmtp_native_status(job->file, &writer) || psmtp_native_status(fd, &reader)) {
            int error = errno;
            (void)close(fd);
            return save_failure(job, "checking temporary file for installation", MTP_FILESYSTEM, error);
        }
        if (!S_ISREG(reader.st_mode) || reader.st_dev != writer.st_dev || reader.st_ino != writer.st_ino) {
            (void)close(fd);
            return save_failure(job, "checking temporary file identity for installation", MTP_VERIFY, 0);
        }
        job->install = calloc(1, sizeof(*job->install));
        if (!job->install) {
            (void)close(fd);
            return MTP_MEMORY;
        }
        job->install->refs = 2; /* USB worker and report owner */
        job->install->fd = fd;
        job->install->size = size;
        job->install->received = job->received;
        strcpy(job->install->name, name);
        __atomic_store_n(&job->report->install, job->install, __ATOMIC_RELEASE);
    }
    (void)now_ms(&job->accepted_ms);
    job->link.native_status = 0;
    fprintf(log, "stage=usb-download-receive-begin size-known=%d expected=%" PRIu64 " mode=%s\n",
        size != UINT64_MAX, size == UINT64_MAX ? 0 : size, job->install_only ? "install" : "save");
    return psmtp_work_progress(job->work, MTP_COPY_RECEIVE, job->received, size == UINT64_MAX ? 0 : size) ? MTP_CANCELLED : MTP_OK;
}

static int begin_file(void *user, const char *name, uint64_t size) {
    return open_file(user, name, size, 0);
}

static int begin_install(void *user, const char *name, uint64_t size) {
    usb_download *job = user;
    if (!job->report) return MTP_UNSUPPORTED;
    job->report->offered = job->report->installing = 1;
    if (!psmtp_pkg_name(name) || size < 0x2000) return MTP_UNSUPPORTED;
    job->install_only = 1;
    return begin_file(user, name, size);
}

static void checkpoint_paths(usb_download *job, const char *name, uint64_t size, int install,
                             const uint8_t key[32]) {
    static const char hex[] = "0123456789abcdef";
    char id[65];
    uint8_t digest[32];
    psmtp_sha256 hash;
    psmtp_sha256_init(&hash);
    psmtp_sha256_update(&hash, key, 32);
    psmtp_sha256_update(&hash, &size, sizeof(size));
    psmtp_sha256_update(&hash, &install, sizeof(install));
    psmtp_sha256_update(&hash, name, strlen(name));
    psmtp_sha256_final(&hash, digest);
    for (size_t i = 0; i < sizeof(digest); ++i) {
        id[2*i] = hex[digest[i] >> 4];
        id[2*i+1] = hex[digest[i] & 15];
    }
    id[64] = 0;
    snprintf(job->part, sizeof(job->part), "%s/.%s.part", PSMTP_DOWNLOAD_DIRECTORY, id);
    snprintf(job->checkpoint, sizeof(job->checkpoint), "%s/.%s.offset", PSMTP_DOWNLOAD_DIRECTORY, id);
}

static int begin_resume(void *user, const char *name, uint64_t size, int install,
                        const uint8_t key[32], uint64_t maximum, uint64_t *offset) {
    usb_download *job = user;
    checkpoint_paths(job, name, size, install, key);
    if (install) {
        if (!job->report || !psmtp_pkg_name(name) || size < 0x2000) return MTP_UNSUPPORTED;
        job->install_only = job->report->installing = 1;
    }
    int rc = open_file(user, name, size, maximum);
    *offset = job->received;
    if (!rc) fprintf(psmtp_work_log(job->work), "stage=download-resume-checkpoint bytes=%" PRIu64 "\n", *offset);
    return rc;
}

static int save_checkpoint(void *user, uint64_t offset) {
    usb_download *job = user;
    char temporary[sizeof(job->checkpoint) + 4];
    if (fsync(job->file)) return save_failure(job, "syncing checkpoint data", file_error(), errno);
    snprintf(temporary, sizeof(temporary), "%s.new", job->checkpoint);
    if (unlink(temporary) && errno != ENOENT) return file_error();
    int fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return file_error();
    int rc = write(fd, &offset, sizeof(offset)) == sizeof(offset) && !fsync(fd) ? MTP_OK : file_error();
    if (close(fd) && !rc) rc = file_error();
    if (!rc && rename(temporary, job->checkpoint)) rc = file_error();
    if (rc) { (void)unlink(temporary); return rc; }
    job->durable = offset;
    if (fsync(job->directory)) return file_error();
    if (job->report) job->report->paused = 1;
    fprintf(psmtp_work_log(job->work), "stage=download-pause-checkpoint bytes=%" PRIu64 "\n", offset);
    return MTP_OK;
}

static int remove_partial(usb_download *job) {
    if (job->file >= 0) {
        int rc = close(job->file);
        job->file = -1;
        if (rc) return file_error();
    }
    if (job->checkpoint[0] && unlink(job->checkpoint) && errno != ENOENT) return file_error();
    if (unlink(job->part) && errno != ENOENT) return file_error();
    job->durable = 0;
    job->temporary_created = 0;
    return fsync(job->directory) ? file_error() : MTP_OK;
}

static int discard_checkpoint(void *user, const char *name, uint64_t size, int install, const uint8_t key[32]) {
    usb_download *job = user;
    job->directory = psmtp_native_directory("/", PSMTP_DOWNLOAD_DIRECTORY);
    if (job->directory < 0) return errno == ENOENT ? MTP_OK : file_error();
    checkpoint_paths(job, name, size, install, key);
    int rc = remove_partial(job);
    job->checkpoint[0] = 0; /* This request owns no transfer for the common cleanup to finish. */
    return rc;
}

static int stop_download(void *user) {
    usb_download *job = user;
    if (job->install) {
        psmtp_usb_install *install = job->install;
        __atomic_store_n(&install->transfer_error, MTP_CANCELLED, __ATOMIC_RELEASE);
        int unclaimed = 0;
        if (!__atomic_compare_exchange_n(&install->claimed, &unclaimed, -1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            uint64_t start, now;
            if (now_ms(&start)) return MTP_IO;
            while (!__atomic_load_n(&install->finished, __ATOMIC_ACQUIRE)) {
                if (cancelled(job)) return MTP_CANCELLED;
                if (now_ms(&now) || now - start >= 10000u) return MTP_TIMEOUT;
                sceKernelUsleep(20000);
            }
            if (install->cleanup) return install->cleanup;
            if (!install->result) return MTP_OK; /* Installation finished before the stop took effect. */
        }
    }
    int rc = remove_partial(job);
    return rc ? rc : PSMTP_DOWNLOAD_STOPPED_LOCAL;
}

static int poll_install_stop(usb_download *job, int *stop) {
    uint8_t command[4];
    size_t got = 0;
    int available = 0, rc = MTP_OK;
    *stop = 0;
#ifndef PSMTP_TEST_USB_DOWNLOAD
    if (job->lan) rc = psmtp_lan_poll_read(job->lan, command, sizeof(command), &available);
    else
#endif
    {
        rc = psmtp_accessory_transfer(&job->link, 0, command, sizeof(command), &got, 50);
        if (rc == MTP_IO && !got && (uint32_t)job->link.native_status == PSMTP_USBD_TIMEOUT) return MTP_OK;
        if (!rc && got && got < sizeof(command)) rc = read_usb(job, command + got, sizeof(command) - got, 0);
        available = got != 0;
    }
    if (rc || !available) return rc;
    if (memcmp(command, "\xff\xff\xff\xfe", sizeof(command))) return MTP_PROTOCOL;
    *stop = 1;
    return MTP_OK;
}

static int install_wire_error(int rc) {
    return rc == MTP_CANCELLED || rc == MTP_NO_SPACE || rc == MTP_VERIFY ? rc : MTP_UNSUPPORTED;
}

static int write_file(void *user, const void *data, size_t length, uint64_t done, uint64_t total) {
    usb_download *job = user;
    const uint8_t *bytes = data;
    if (job->install) {
        int rc = install_validate(job->install);
        if (rc) return install_wire_error(rc);
    }
    while (length) {
        if (cancelled(job)) return MTP_CANCELLED;
        ssize_t count = write(job->file, bytes, length);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return save_failure(job, "writing temporary file", count ? file_error() : MTP_FILESYSTEM, count ? errno : 0);
        bytes += count;
        length -= (size_t)count;
    }
    job->received = done;
    if (job->install) __atomic_store_n(&job->install->received, done, __ATOMIC_RELEASE);
    return psmtp_work_progress(job->work, MTP_COPY_RECEIVE, done, total == UINT64_MAX ? 0 : total) ? MTP_CANCELLED : MTP_OK;
}

static int same_file(const psmtp_native_stat *a, const psmtp_native_stat *b) {
    return S_ISREG(b->st_mode) && a->st_dev == b->st_dev && a->st_ino == b->st_ino && a->st_size == b->st_size;
}

static int path_is_file(const char *path, const psmtp_native_stat *expected) {
    psmtp_native_stat actual;
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return 0;
    int matches = !psmtp_native_status(fd, &actual) && same_file(expected, &actual);
    return close(fd) == 0 && matches;
}

static int publish_file(void *user, uint64_t size, const uint8_t digest[32]) {
    usb_download *job = user;
    FILE *log = psmtp_work_log(job->work);
    psmtp_native_stat source, reserved;
    int rc, target, have_reservation;
    uint64_t now = 0;
    (void)digest; /* Phone-streamed transfers verify SHA-256 before publication. */
    if (!now_ms(&now) && job->accepted_ms && now >= job->accepted_ms)
        fprintf(log, "stage=usb-download-receive-end bytes=%" PRIu64 " elapsed-ms=%" PRIu64 " source=%s\n",
            size, now - job->accepted_ms, job->source ? "https" : "verified-phone-stream");
    if (psmtp_work_progress(job->work, MTP_COPY_PUBLISH, size, size)) return MTP_CANCELLED;
    if (psmtp_native_status(job->file, &source)) return save_failure(job, "checking temporary file", file_error(), errno);
    if (!S_ISREG(source.st_mode) || (uint64_t)source.st_size != size)
        return save_failure(job, "checking temporary file size", MTP_VERIFY, 0);
    if (job->install) {
        uint64_t started, heartbeat;
        if (now_ms(&started)) return MTP_IO;
        heartbeat = started;
        __atomic_store_n(&job->install->verified, 1, __ATOMIC_RELEASE);
        while (!__atomic_load_n(&job->install->finished, __ATOMIC_ACQUIRE)) {
            if (cancelled(job)) return MTP_CANCELLED;
            if (now_ms(&now)) return MTP_IO;
            if (now - started >= 3600000u) return MTP_TIMEOUT;
            if (!job->source && now - heartbeat >= 1000u) {
                int stop;
                rc = reply_usb(job, PSMTP_INSTALL_WAIT_REPLY, 8);
                if (rc) return rc;
                rc = poll_install_stop(job, &stop);
                if (rc) return rc;
                if (stop) {
                    rc = stop_download(job);
                    if (rc) return rc;
                    break;
                }
                heartbeat = now;
            }
            sceKernelUsleep(20000);
        }
        rc = job->install->result;
        /* Remove the staged source before acknowledging a completed install. */
        if (!rc) {
            job->durable = 0; /* Native install completed, including when the final ACK is lost. */
            if (unlink(job->part))
                fprintf(log, "stage=usb-direct-install-cleanup remove-errno=%d will-retry=1\n", errno);
            else job->temporary_created = 0;
        }
        return rc ? install_wire_error(rc) : MTP_OK;
    }
    rc = psmtp_native_output_access(job->file, 0, 0, log);
    if (rc) return save_failure(job, "setting file permissions", rc, errno);
    if (fsync(job->file)) return save_failure(job, "syncing temporary file", file_error(), errno);
    if (!path_is_file(job->part, &source)) return save_failure(job, "checking temporary file identity", MTP_VERIFY, 0);
    rc = close(job->file);
    job->file = -1;
    if (rc) return save_failure(job, "closing temporary file", file_error(), errno);
    if (cancelled(job)) return MTP_CANCELLED;

    /* Like native file rename, reserve the final name exclusively. The
     * rename replaces only the zero-length entry owned by this job. */
    target = open(job->final, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (target < 0) return save_failure(job, "reserving filename", errno == EEXIST ? MTP_EXISTS : file_error(), errno);
    have_reservation = !psmtp_native_status(target, &reserved) && S_ISREG(reserved.st_mode) && !reserved.st_size;
    rc = close(target);
    if (rc) rc = save_failure(job, "closing filename reservation", file_error(), errno);
    else if (!have_reservation || !path_is_file(job->final, &reserved))
        rc = save_failure(job, "checking filename reservation", MTP_VERIFY, 0);
    else if (cancelled(job)) rc = MTP_CANCELLED;
    else if (rename(job->part, job->final)) rc = save_failure(job, "renaming verified file", file_error(), errno);
    if (rc) {
        if (have_reservation && path_is_file(job->final, &reserved) && unlink(job->final))
            fprintf(log, "stage=usb-download-save operation=remove-reservation errno=%d\n", errno);
        return rc;
    }

    job->published = 1;
    job->temporary_created = 0;
    int sync_error = fsync(job->directory) ? errno : 0;
    fprintf(log, "stage=usb-download-publish bytes=%" PRIu64 " method=reserved-rename directory-sync-errno=%d\n", size, sync_error);
    return MTP_OK;
}

static int open_link(void *user, const char *name, uint64_t size, int install, const psmtp_download_link *link) {
    usb_download *job = user;
    if (job->report) {
        job->report->offered = 1;
        job->report->installing = install;
    }
    int rc = psmtp_https_open_link(&job->source, link, job->work, &size);
    if (rc) {
        int http = psmtp_https_http_status(job->source);
        if (rc == MTP_RESPONSE && http >= 100 && http <= 599) job->source_error = PSMTP_DOWNLOAD_HTTP_STATUS + (uint32_t)http;
        else if (rc == MTP_NO_INTERNET) job->source_error = PSMTP_DOWNLOAD_NO_INTERNET;
        else if (rc == MTP_TIMEOUT) job->source_error = PSMTP_DOWNLOAD_SOURCE_TIMEOUT;
        else if (rc == MTP_VERIFY || rc == MTP_PROTOCOL || rc == MTP_RESPONSE) job->source_error = PSMTP_DOWNLOAD_SOURCE_RESPONSE;
        else if (rc == MTP_STATE) job->source_error = PSMTP_DOWNLOAD_SOURCE_BUSY;
        else if (rc == MTP_UNSUPPORTED) job->source_error = PSMTP_DOWNLOAD_SOURCE_UNAVAILABLE;
        else if (rc != MTP_CANCELLED) job->source_error = PSMTP_DOWNLOAD_SOURCE_CONNECT;
        if (job->report) job->report->source_error = job->source_error;
        fprintf(psmtp_work_log(job->work), "stage=download-link-open status=%d http=%d detail=%u\n", rc, http, job->source_error);
        return rc;
    }
    if (install) {
        uint8_t header[0x440];
        size_t offset = 0;
        while (offset < sizeof(header)) {
            size_t n;
            rc = psmtp_https_read(job->source, header + offset, sizeof(header) - offset, &n);
            if (rc || !n) {
                if (rc != MTP_CANCELLED) job->source_error = rc == MTP_NO_INTERNET ? PSMTP_DOWNLOAD_NO_INTERNET :
                    rc == MTP_TIMEOUT ? PSMTP_DOWNLOAD_SOURCE_TIMEOUT :
                    rc ? PSMTP_DOWNLOAD_SOURCE_CONNECT : PSMTP_DOWNLOAD_SOURCE_PKG;
                if (job->report) job->report->source_error = job->source_error;
                return rc ? rc : MTP_VERIFY;
            }
            offset += n;
        }
        uint64_t package_size = 0;
        for (unsigned i = 0; i < 8; ++i) package_size = (package_size << 8) | header[0x430 + i];
        if (memcmp(header, "\x7f" "CNT", 4) || package_size < 0x2000 || package_size > PSMTP_DOWNLOAD_MAX ||
            (size != UINT64_MAX && package_size != size)) {
            job->source_error = PSMTP_DOWNLOAD_SOURCE_PKG;
            if (job->report) job->report->source_error = job->source_error;
            return MTP_VERIFY;
        }
        size = package_size;
        rc = begin_install(job, name, size);
        if (!rc) rc = write_file(job, header, sizeof(header), sizeof(header), size);
    } else rc = begin_file(job, name, size);
    job->link_size = size;
    fprintf(psmtp_work_log(job->work), "stage=download-link-destination status=%d install=%d bytes=%" PRIu64 "\n",
            rc, install, size);
    return rc;
}

static uint32_t download_error_status(void *user, int status) {
    usb_download *job = user;
    if (status == MTP_CANCELLED || status == MTP_NO_SPACE || status == MTP_EXISTS) return 0;
    if (job->source_error) return job->source_error;
    return job->install ? __atomic_load_n(&job->install->wire_error, __ATOMIC_ACQUIRE) : 0;
}

static int run_link(void *user) {
    usb_download *job = user;
    uint8_t *buffer = malloc(PSMTP_DOWNLOAD_CHUNK);
    if (!buffer) return MTP_MEMORY;
    int rc;
    for (;;) {
        size_t count;
        rc = psmtp_https_read(job->source, buffer, PSMTP_DOWNLOAD_CHUNK, &count);
        if (rc || !count) break;
        uint64_t limit = job->link_size == UINT64_MAX ? PSMTP_DOWNLOAD_MAX : job->link_size;
        if (count > limit - job->received) { rc = MTP_VERIFY; break; }
        rc = write_file(job, buffer, count, job->received + count, job->link_size);
        if (rc) break;
    }
    free(buffer);
    if (!rc && job->link_size != UINT64_MAX && job->received != job->link_size) rc = MTP_VERIFY;
    if (!rc) rc = publish_file(job, job->received, NULL);
    return rc;
}

static int query_capacity(void *user, uint64_t *available, uint64_t *total) {
    usb_download *job = user;
    FILE *log = psmtp_work_log(job->work);
    job->capacity_query = 1;
    int rc = psmtp_native_capacity("/", PSMTP_DOWNLOAD_DIRECTORY, available, total, log);
    if (rc == MTP_FILESYSTEM) rc = psmtp_native_capacity("/", PSMTP_LOG_DIRECTORY, available, total, log);
    return rc;
}

static void record_parts(void *user, unsigned count, uint64_t speed, const uint8_t parts[PSMTP_DOWNLOAD_PARTS_MAX]) {
    usb_download *job = user;
    if (!job->report) return;
    uint64_t percentages = 0;
    for (unsigned i = 0; i < count; ++i) percentages |= (uint64_t)parts[i] << (8 * i);
    __atomic_store_n(&job->report->source_speed, speed, __ATOMIC_RELAXED);
    __atomic_store_n(&job->report->part_percentages, percentages, __ATOMIC_RELAXED);
    __atomic_store_n(&job->report->part_count, count, __ATOMIC_RELEASE);
}

int psmtp_usb_download_run(void *user, psmtp_work *work) {
    psmtp_usb_download_report *report = user;
    usb_download job = {.work = work, .report = report, .directory = -1, .file = -1};
    const psmtp_download_receiver receiver = {
        &job, read_usb, reply_usb, begin_file, write_file, publish_file, begin_install, query_capacity,
        begin_resume, save_checkpoint, stop_download, discard_checkpoint,
        open_link, run_link, download_error_status, record_parts
    };
    strcpy(job.part, download_part);
    FILE *log = psmtp_work_log(work);
    psmtp_ps4_candidate devices[PSMTP_WATCH_MAX];
    size_t count = 0;
    int rc, cleanup;
    if (psmtp_work_cancelled(work)) return MTP_CANCELLED;
#ifndef PSMTP_TEST_USB_DOWNLOAD
    if(report && report->wireless){
        rc=psmtp_lan_open(&job.lan,work,report->lan_address,report->lan_qr,&report->pairing);
        fprintf(log,"stage=lan-download-listen status=%d tls=1\n",rc);
        if(!rc){
            __atomic_store_n(&report->ready,1,__ATOMIC_RELEASE);
            do {
                job.capacity_query=0;
                rc=psmtp_lan_accept(job.lan);
                if(!rc)rc=psmtp_download_receive(&receiver);
            } while(!rc && job.capacity_query);
        }
        if(rc)fprintf(log,"stage=lan-download-transfer status=%d received=%" PRIu64 "\n",rc,job.received);
        cleanup=psmtp_lan_close(job.lan);
        goto files;
    }
#endif
    /* The accessory path owns USB discovery until the worker is joined. */
    (void)psmtp_ps4_poll_devices(devices, &count, 0, log);
    rc = psmtp_accessory_open(&job.link, cancelled, &job, log);
    if (rc) {
        if (report) {
            report->setup_stage = job.link.stage;
            report->native_status = job.link.native_status;
        }
        fprintf(log, "stage=usb-download-setup status=%d operation=%s native=0x%08" PRIx32 "\n",
            rc, job.link.stage ? job.link.stage : "unknown", (uint32_t)job.link.native_status);
    } else {
        fputs("stage=usb-download-setup ready=1 waiting-for-android-offer=1\n", log);
        if (report) __atomic_store_n(&report->ready, 1, __ATOMIC_RELEASE);
        do {
            job.capacity_query = 0;
            rc = psmtp_download_receive(&receiver);
        } while (!rc && job.capacity_query);
        if (rc) fprintf(log, "stage=usb-download-transfer status=%d received=%" PRIu64 " last-usb-error=0x%08" PRIx32 "\n",
            rc, job.received, (uint32_t)job.link.native_status);
    }
    cleanup = psmtp_accessory_close(&job.link);
#ifndef PSMTP_TEST_USB_DOWNLOAD
files:
#endif
    if (job.source) {
        int closed = psmtp_https_close(job.source);
        if (!cleanup) cleanup = closed;
    }
    if (job.install) {
        if (rc) __atomic_store_n(&job.install->transfer_error, rc, __ATOMIC_RELEASE);
        psmtp_usb_install_release(job.install);
    }
    if (job.file >= 0 && close(job.file) && !cleanup) cleanup = file_error();
    if (job.temporary_created && !job.durable && unlink(job.part) && !cleanup) cleanup = file_error();
    if ((job.published || (!rc && job.install_only) || !job.durable) && job.checkpoint[0])
        (void)unlink(job.checkpoint);
    if (job.directory >= 0 && close(job.directory) && !cleanup) cleanup = file_error();
    fprintf(log, "stage=usb-download status=%d cleanup=%d published=%d\n", rc, cleanup, job.published);
    return rc ? rc : job.published || job.install_only ? MTP_OK : cleanup;
}
