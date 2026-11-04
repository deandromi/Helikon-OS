/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Host harness for the read-only HFS+ firmware loader. It opens a disposable
 * HFS+ image, reads the manifest/image/bootstate through the HFS+ core and
 * prints the verification result. It never writes the image.
 */
#include "firmware_loader.h"
#include "hfs_loader.h"
#include "loader_error.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SCRATCH_SIZE 32768u
#define IMAGE_CAPACITY (1024u * 1024u)

struct io {
    int fd;
};

static int read_at(void *context, uint64_t offset, void *buffer, size_t length)
{
    struct io *io = context;
    ssize_t done = pread(io->fd, buffer, length, (off_t)offset);
    return done == (ssize_t)length ? 0 : -1;
}

static int signature_verify(void *context, uint32_t key_id,
                            const uint8_t *message, size_t message_length,
                            const uint8_t *signature, size_t signature_length)
{
    (void)context;
    (void)message;
    (void)signature;
    return key_id == 7 && message_length == HELIKON_MANIFEST_SIGNED_SIZE &&
           signature_length == HELIKON_MANIFEST_SIGNATURE_SIZE ? 0 : -1;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s IMAGE\n", argv[0]);
        return 2;
    }

    struct io io;
    io.fd = open(argv[1], O_RDONLY);
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

    struct hfs_volume volume;
    if (hfs_volume_probe(&volume, read_at, &io, 0, (uint64_t)end) != HFS_OK) {
        fprintf(stderr, "probe failed\n");
        close(io.fd);
        return 1;
    }

    static uint8_t catalog_scratch[SCRATCH_SIZE];
    static uint8_t extents_scratch[SCRATCH_SIZE];
    struct hfs_catalog catalog;
    if (hfs_catalog_open(&catalog, &volume, catalog_scratch,
                         sizeof(catalog_scratch)) != HFS_OK) {
        fprintf(stderr, "catalog open failed\n");
        close(io.fd);
        return 1;
    }
    struct hfs_extents extents;
    int have_extents = hfs_extents_open(&extents, &volume, extents_scratch,
                                        sizeof(extents_scratch)) == HFS_OK;
    struct helikon_hfs_loader hfs_loader = {
        &catalog, have_extents ? &extents : NULL
    };
    static uint8_t image[IMAGE_CAPACITY];
    struct helikon_loader_result result;
    int rc = helikon_loader_run(
        helikon_hfs_read_file, &hfs_loader,
        "/.helikon/firmware.manifest", "/.helikon/firmware.img",
        "/.helikon/bootstate", HELIKON_BOARD_IPOD6G, 0, signature_verify,
        NULL, image, sizeof(image), &result);
    close(io.fd);

    if (rc != HELIKON_LOADER_OK) {
        fprintf(stderr, "loader: %s (%d)\n", helikon_loader_strerror(rc), rc);
        return 1;
    }
    printf("loader: OK\n");
    printf("image version: %u\n", result.manifest.image_version);
    printf("load address: 0x%08x\n", result.manifest.load_address);
    printf("image size: %u\n", result.manifest.image_size);
    printf("bootstate: %s\n",
           result.bootstate_valid ? "GOOD" : "DEFAULT");
    return 0;
}
