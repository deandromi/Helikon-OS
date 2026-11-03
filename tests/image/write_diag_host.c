/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Host harness for the write-diagnostic core. It runs the same inner-image
 * sequence as the bootloader against a disposable outer HFS+ image on macOS.
 */
#include "write_diag.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
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

static int write_at(void *context, uint64_t offset, const void *buffer,
                    size_t length)
{
    struct io *io = context;
    ssize_t done = pwrite(io->fd, buffer, length, (off_t)offset);
    return done == (ssize_t)length ? 0 : -1;
}

static int flush_at(void *context)
{
    struct io *io = context;
    return fsync(io->fd) == 0 ? 0 : -1;
}

static const char *status_name(enum boot_write_status status)
{
    switch (status) {
    case BOOT_WRITE_PASS: return "PASS";
    case BOOT_WRITE_FAIL: return "FAIL";
    case BOOT_WRITE_UNSUPPORTED: return "UNSUPPORTED";
    case BOOT_WRITE_NOT_TESTED: return "NOT TESTED";
    default: return "UNKNOWN";
    }
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: %s OUTER_IMAGE [read-cut]\n", argv[0]);
        return 2;
    }
    int read_cut = argc == 3 && strcmp(argv[2], "read-cut") == 0;

    struct io io;
    io.fd = open(argv[1], O_RDWR);
    if (io.fd < 0) {
        perror("open");
        return 2;
    }
    off_t end = lseek(io.fd, 0, SEEK_END);
    if (end < 0) {
        perror("lseek");
        return 2;
    }

    static uint8_t outer_catalog_scratch[SCRATCH_SIZE];
    static uint8_t outer_extents_scratch[SCRATCH_SIZE];
    static uint8_t inner_catalog_scratch[SCRATCH_SIZE];
    struct boot_write_diag_result result;
    uint32_t cut_at = 0;
    int rc = boot_write_diag_run(read_at, write_at, flush_at, &io, 0,
                                 (uint64_t)end, "helikon-test.img",
                                 outer_catalog_scratch,
                                 sizeof(outer_catalog_scratch),
                                 outer_extents_scratch,
                                 sizeof(outer_extents_scratch),
                                 inner_catalog_scratch,
                                 sizeof(inner_catalog_scratch), 0,
                                 read_cut ? &cut_at : NULL, &result);
    close(io.fd);
    if (rc != HFS_OK) {
        fprintf(stderr, "write diagnostic: %s\n", hfs_strerror(rc));
        return 1;
    }

    printf("outer volume:  %s\n", status_name(result.outer_volume));
    printf("test file:     %s\n", status_name(result.test_file));
    printf("inner volume:  %s (%u-byte blocks, %u blocks, %llu bytes)\n",
           status_name(result.inner_volume), result.inner_block_size,
           result.inner_blocks, (unsigned long long)result.inner_size);
    printf("inner catalog: %s\n", status_name(result.inner_catalog));
    printf("inner journal: %s\n", status_name(result.inner_journal));
    printf("cleanup:       %s\n", status_name(result.cleanup));
    printf("recovered:     %s\n", result.recovered ? "yes" : "no");
    printf("create folder: %s\n", status_name(result.create_folder));
    printf("create file:   %s\n", status_name(result.create_file));
    printf("append:        %s\n", status_name(result.append));
    printf("readback:      %s\n", status_name(result.readback));
    printf("rename:        %s\n", status_name(result.rename));
    printf("truncate:      %s\n", status_name(result.truncate));
    printf("delete:        %s\n", status_name(result.delete));
    printf("inner writes:  %llu\n", (unsigned long long)result.writes);
    if (read_cut)
        printf("cut target:    %u\n", result.cut_at);

    if (result.error != HFS_OK)
        printf("error: %s (%d)\n", hfs_strerror(result.error), result.error);

    if (result.outer_volume != BOOT_WRITE_PASS ||
        result.test_file != BOOT_WRITE_PASS ||
        result.inner_volume != BOOT_WRITE_PASS ||
        result.inner_catalog != BOOT_WRITE_PASS ||
        result.inner_journal != BOOT_WRITE_PASS ||
        result.cleanup != BOOT_WRITE_PASS ||
        result.create_folder != BOOT_WRITE_PASS ||
        result.create_file != BOOT_WRITE_PASS ||
        result.append != BOOT_WRITE_PASS ||
        result.readback != BOOT_WRITE_PASS ||
        result.truncate != BOOT_WRITE_PASS ||
        result.delete != BOOT_WRITE_PASS)
        return 1;
    if (result.rename != BOOT_WRITE_PASS)
        return 1;
    return 0;
}
