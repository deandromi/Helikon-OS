/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Host harness for the read-only HFS+ firmware loader. It opens a disposable
 * HFS+ image, reads the manifest/image/bootstate through the HFS+ core and
 * verifies the manifest signature against a public key supplied by the test
 * driver. It never writes the image.
 */
#include "firmware_loader.h"
#include "hfs_loader.h"
#include "keys.h"
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

static int nibble(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static int parse_public_key(const char *hex,
                            uint8_t out[HELIKON_ED25519_PUBLIC_KEY_SIZE])
{
    if (strlen(hex) != HELIKON_ED25519_PUBLIC_KEY_SIZE * 2u)
        return -1;
    for (size_t i = 0; i < HELIKON_ED25519_PUBLIC_KEY_SIZE; ++i) {
        int high = nibble(hex[i * 2u]);
        int low = nibble(hex[i * 2u + 1u]);
        if (high < 0 || low < 0)
            return -1;
        out[i] = (uint8_t)((high << 4) | low);
    }
    return 0;
}

static int parse_u32(const char *text, uint32_t *value)
{
    char *end = NULL;
    unsigned long parsed = strtoul(text, &end, 0);
    if (end == text || *end != '\0' || parsed > 0xfffffffful)
        return -1;
    *value = (uint32_t)parsed;
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 4 && argc != 5) {
        fprintf(stderr, "usage: %s IMAGE PUBLIC_KEY_HEX KEY_ID"
                        " [ALLOW_DEVELOPMENT]\n", argv[0]);
        return 2;
    }
    uint32_t development = 0;
    if (argc == 5 && parse_u32(argv[4], &development) != 0) {
        fprintf(stderr, "bad development flag\n");
        return 2;
    }
    int allow_development = development != 0;

    struct helikon_public_key public_key;
    if (parse_public_key(argv[2], public_key.key) != 0) {
        fprintf(stderr, "bad public key\n");
        return 2;
    }
    if (parse_u32(argv[3], &public_key.id) != 0) {
        fprintf(stderr, "bad key id\n");
        return 2;
    }
    struct helikon_key_table key_table = { &public_key, 1 };

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
        "/.helikon/bootstate", HELIKON_BOARD_IPOD6G, allow_development,
        helikon_key_table_verify, &key_table, image, sizeof(image), &result);
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
