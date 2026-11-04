/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Host harness for the outer-volume write diagnostic. It runs the same core as
 * the bootloader against a disposable outer HFS+ image on macOS.
 */
#include "outer_diag.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define SCRATCH_SIZE 32768u

struct io {
    int fd;
};

static int read_at(void *context, uint64_t offset, void *buffer, size_t length)
{
    struct io *io = context;
    ssize_t done = pread(io->fd, buffer, length, (off_t)offset);
    return done == (ssize_t)length ? 0 : -1;
}

static unsigned long ata_count;
static int write_at(void *context, uint64_t offset, const void *buffer,
                    size_t length)
{
    struct io *io = context;
    ssize_t done = pwrite(io->fd, buffer, length, (off_t)offset);
    if (done != (ssize_t)length)
        return -1;
    unsigned long sectors = (unsigned long)((offset + length + 511u) / 512u -
                                            offset / 512u);
    ata_count += sectors;
    fprintf(stderr, "WRITE off=%llu len=%zu ata=%lu\n",
            (unsigned long long)offset, length, ata_count);
    return 0;
}

static int flush_at(void *context)
{
    struct io *io = context;
    return fsync(io->fd) == 0 ? 0 : -1;
}

static const char *status_name(enum boot_outer_status status)
{
    switch (status) {
    case BOOT_OUTER_PASS: return "PASS";
    case BOOT_OUTER_FAIL: return "FAIL";
    case BOOT_OUTER_UNSUPPORTED: return "UNSUPPORTED";
    case BOOT_OUTER_NOT_TESTED: return "NOT TESTED";
    default: return "UNKNOWN";
    }
}

static int make_dirty(struct io *io, uint64_t length)
{
    struct hfs_volume volume;
    int rc = hfs_volume_probe(&volume, read_at, io, 0, length);
    if (rc != HFS_OK) {
        fprintf(stderr, "probe: %s\n", hfs_strerror(rc));
        return 1;
    }
    struct hfs_journal journal;
    rc = hfs_journal_open(&journal, &volume, write_at, flush_at);
    if (rc != HFS_OK) {
        fprintf(stderr, "journal: %s\n", hfs_strerror(rc));
        return 1;
    }
    if (!hfs_journal_is_clean(&journal)) {
        fprintf(stderr, "journal is already dirty\n");
        return 1;
    }

    uint8_t header[512];
    if (read_at(io, volume.base + HFS_VOLUME_HEADER_OFFSET, header,
                sizeof(header)) != 0) {
        fprintf(stderr, "read: I/O error\n");
        return 1;
    }
    struct hfs_journal_entry entry;
    entry.sector = HFS_VOLUME_HEADER_OFFSET / 512u;
    entry.data = header;
    entry.size = sizeof(header);
    rc = hfs_journal_append(&journal, &entry, 1);
    if (rc != HFS_OK) {
        fprintf(stderr, "append: %s\n", hfs_strerror(rc));
        return 1;
    }
    printf("dirty journal written (one committed, unreplayed block)\n");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3 ||
        (argc == 3 && strcmp(argv[2], "dirty") != 0)) {
        fprintf(stderr, "usage: %s OUTER_IMAGE [dirty]\n", argv[0]);
        return 2;
    }

    struct io io;
    io.fd = open(argv[1], O_RDWR);
    if (io.fd < 0) {
        perror("open");
        return 2;
    }
    off_t end = lseek(io.fd, 0, SEEK_END);
    if (end < 0) {
        perror("lseek");
        close(io.fd);
        return 2;
    }

    if (argc == 3) {
        int dirty_rc = make_dirty(&io, (uint64_t)end);
        close(io.fd);
        return dirty_rc;
    }

    static uint8_t catalog_scratch[SCRATCH_SIZE];
    struct boot_outer_diag_result result;
    time_t now = time(NULL);
    uint32_t hfs_now = 0;
    if (now > 0)
        hfs_now = (uint32_t)((int64_t)now + 2082844800LL);

    ata_count = 0;
    int rc = boot_outer_diag_run(read_at, write_at, flush_at, &io, 0,
                                 (uint64_t)end, catalog_scratch,
                                 sizeof(catalog_scratch), hfs_now, NULL,
                                 NULL, NULL, &result);
    close(io.fd);
    if (rc != HFS_OK) {
        fprintf(stderr, "outer diagnostic: %s\n", hfs_strerror(rc));
        return 1;
    }

    printf("opt-in:         %s\n", status_name(result.opt_in));
    printf("outer volume:   %s\n", status_name(result.outer_volume));
    printf("outer journal:  %s\n", status_name(result.outer_journal));
    printf("catalog:        %s\n", status_name(result.catalog));
    printf("cleanup:        %s\n", status_name(result.cleanup));
    printf("create folder:  %s\n", status_name(result.create_folder));
    printf("create file:    %s\n", status_name(result.create_file));
    printf("append:         %s\n", status_name(result.append));
    printf("readback:       %s\n", status_name(result.readback));
    printf("rename:         %s\n", status_name(result.rename));
    printf("truncate:       %s\n", status_name(result.truncate));
    printf("delete file:    %s\n", status_name(result.delete_file));
    printf("delete folder:  %s\n", status_name(result.delete_folder));
    printf("restored:       %s\n", status_name(result.restored));
    printf("free blocks:    %u -> %u\n", result.free_blocks_before,
           result.free_blocks_after);
    printf("writes:         %lu\n", ata_count);
    if (result.error != HFS_OK)
        printf("error:          %s (%d)\n", hfs_strerror(result.error),
               result.error);

    if (result.opt_in != BOOT_OUTER_PASS ||
        result.outer_volume != BOOT_OUTER_PASS ||
        result.outer_journal != BOOT_OUTER_PASS ||
        result.catalog != BOOT_OUTER_PASS ||
        result.cleanup != BOOT_OUTER_PASS ||
        result.create_folder != BOOT_OUTER_PASS ||
        result.create_file != BOOT_OUTER_PASS ||
        result.append != BOOT_OUTER_PASS ||
        result.readback != BOOT_OUTER_PASS ||
        result.rename != BOOT_OUTER_PASS ||
        result.truncate != BOOT_OUTER_PASS ||
        result.delete_file != BOOT_OUTER_PASS ||
        result.delete_folder != BOOT_OUTER_PASS ||
        result.restored != BOOT_OUTER_PASS)
        return 1;
    return 0;
}
