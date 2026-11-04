/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "firmware_loader.h"
#include "keys.h"
#include "loader_error.h"
#include "sha256.h"
#include "test_check.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define IMAGE_SIZE 64u
#define MANIFEST_PATH "/.helikon/firmware.manifest"
#define IMAGE_PATH "/.helikon/firmware.img"
#define BOOTSTATE_PATH "/.helikon/bootstate"

static void put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void make_image(uint8_t image[IMAGE_SIZE])
{
    for (size_t i = 0; i < IMAGE_SIZE; ++i)
        image[i] = (uint8_t)(i * 3u + 1u);
}

static void make_manifest(uint8_t manifest[HELIKON_MANIFEST_SIZE],
                          uint32_t board, uint32_t image_version,
                          uint32_t load_address, uint32_t image_size,
                          const uint8_t *image, uint32_t key_id,
                          uint32_t flags)
{
    memset(manifest, 0, HELIKON_MANIFEST_SIZE);
    memcpy(manifest, HELIKON_MANIFEST_MAGIC, 8u);
    put32(manifest + 8, HELIKON_MANIFEST_VERSION);
    put32(manifest + 12, board);
    put32(manifest + 16, image_version);
    put32(manifest + 20, load_address);
    put32(manifest + 24, image_size);
    put32(manifest + 28, key_id);
    helikon_sha256(image, image_size, manifest + 32);
    put32(manifest + 64, flags);
    memset(manifest + HELIKON_MANIFEST_SIGNED_SIZE, 0xa5,
           HELIKON_MANIFEST_SIGNATURE_SIZE);
}

static void make_slot(uint8_t *slot, uint32_t sequence, uint32_t board,
                      uint32_t image_version, uint32_t load_address,
                      uint32_t image_size, const uint8_t *image,
                      uint8_t state)
{
    memset(slot, 0, HELIKON_BOOTSTATE_SLOT_SIZE);
    memcpy(slot, HELIKON_BOOTSTATE_MAGIC, 4u);
    put32(slot + 4, HELIKON_BOOTSTATE_VERSION);
    put32(slot + 8, sequence);
    put32(slot + 12, board);
    put32(slot + 16, image_version);
    put32(slot + 20, load_address);
    put32(slot + 24, image_size);
    helikon_sha256(image, image_size, slot + 28);
    slot[60] = state;
    helikon_sha256(slot, 64u, slot + 64);
}

struct memory_files {
    uint8_t manifest[HELIKON_MANIFEST_SIZE];
    uint8_t image[IMAGE_SIZE];
    uint8_t bootstate[HELIKON_BOOTSTATE_SIZE];
    size_t bootstate_size;
    int has_manifest;
    int has_image;
    int has_bootstate;
    int reject_signature;
    uint32_t rejected_key;
    int image_reads;
};

static int memory_read(void *context, const char *path, uint64_t offset,
                       void *buffer, size_t length, size_t *done)
{
    struct memory_files *files = context;
    const uint8_t *source = NULL;
    size_t source_size = 0;
    if (strcmp(path, MANIFEST_PATH) == 0 && files->has_manifest) {
        source = files->manifest;
        source_size = sizeof(files->manifest);
    } else if (strcmp(path, IMAGE_PATH) == 0 && files->has_image) {
        ++files->image_reads;
        source = files->image;
        source_size = sizeof(files->image);
    } else if (strcmp(path, BOOTSTATE_PATH) == 0 && files->has_bootstate) {
        source = files->bootstate;
        source_size = files->bootstate_size;
    } else {
        return HELIKON_LOADER_ERR_NOT_FOUND;
    }
    if (offset > source_size || length > source_size - offset)
        return HELIKON_LOADER_ERR_IO;
    memcpy(buffer, source + offset, length);
    *done = length;
    return 0;
}

static int signature_verify(void *context, uint32_t key_id,
                            const uint8_t *message, size_t message_length,
                            const uint8_t *signature,
                            size_t signature_length)
{
    struct memory_files *files = context;
    (void)message;
    (void)signature;
    if (message_length != HELIKON_MANIFEST_SIGNED_SIZE ||
        signature_length != HELIKON_MANIFEST_SIGNATURE_SIZE)
        return -1;
    if (files->reject_signature && key_id == files->rejected_key)
        return -1;
    return 0;
}

static void make_valid_files(struct memory_files *files)
{
    memset(files, 0, sizeof(*files));
    make_image(files->image);
    make_manifest(files->manifest, HELIKON_BOARD_IPOD6G, 1,
                  HELIKON_DRAM_BASE, IMAGE_SIZE, files->image, 7, 0);
    files->has_manifest = 1;
    files->has_image = 1;
    files->has_bootstate = 1;
    files->bootstate_size = sizeof(files->bootstate);
    memset(files->bootstate, 0, sizeof(files->bootstate));
    make_slot(files->bootstate, 1, HELIKON_BOARD_IPOD6G, 1,
              HELIKON_DRAM_BASE, IMAGE_SIZE, files->image,
              HELIKON_BOOTSTATE_GOOD);
}

static int load(struct memory_files *files, uint8_t *image,
                struct helikon_loader_result *result)
{
    return helikon_loader_run(memory_read, files, MANIFEST_PATH, IMAGE_PATH,
                              BOOTSTATE_PATH, HELIKON_BOARD_IPOD6G, 0,
                              signature_verify, files, image, IMAGE_SIZE,
                              result);
}

static int test_sha256_vectors(void)
{
    uint8_t digest[HELIKON_SHA256_SIZE];
    helikon_sha256("", 0, digest);
    static const uint8_t empty[32] = {
        0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14,
        0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
        0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c,
        0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55
    };
    CHECK(memcmp(digest, empty, sizeof(empty)) == 0);
    helikon_sha256("abc", 3, digest);
    static const uint8_t abc[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
    };
    CHECK(memcmp(digest, abc, sizeof(abc)) == 0);
    return 0;
}

/*
 * A fixed manifest signed once with a throwaway Ed25519 key. Only the public
 * key and signature are retained. The payload is make_image() above.
 */
static const uint8_t signed_public_key[HELIKON_ED25519_PUBLIC_KEY_SIZE] = {
    0x60, 0x88, 0xe8, 0x10, 0xba, 0x83, 0x9f, 0xe2,
    0x73, 0xb8, 0x0d, 0x6f, 0x01, 0xbf, 0xe5, 0x02,
    0x36, 0x79, 0xa0, 0x19, 0xe7, 0x0e, 0x03, 0x52,
    0xf4, 0x65, 0xba, 0xc0, 0x76, 0xf0, 0x0c, 0xa4
};

static const uint8_t signed_manifest_signature[HELIKON_ED25519_SIGNATURE_SIZE] = {
    0x95, 0xf3, 0xe8, 0x48, 0x88, 0x93, 0xdb, 0xaf,
    0x57, 0xd1, 0xc1, 0x24, 0x72, 0x7c, 0xa0, 0xd6,
    0xbe, 0xc8, 0x02, 0xa7, 0x55, 0xaf, 0xdf, 0x94,
    0x77, 0xbe, 0x36, 0xea, 0x18, 0x1a, 0x85, 0xb3,
    0x2e, 0x59, 0x71, 0x9f, 0x7e, 0x98, 0x10, 0x66,
    0x96, 0x97, 0x0d, 0xa9, 0x00, 0x0a, 0xf5, 0x9b,
    0xcd, 0xa8, 0x69, 0x2b, 0x01, 0x5b, 0x0e, 0x9b,
    0x90, 0x74, 0xcd, 0xb2, 0xb9, 0x00, 0xe3, 0x09
};

static int load_signed(struct memory_files *files, uint8_t *image,
                       struct helikon_loader_result *result)
{
    struct helikon_public_key key;
    memset(&key, 0, sizeof(key));
    key.id = 7;
    memcpy(key.key, signed_public_key, sizeof(key.key));
    struct helikon_key_table table = { &key, 1 };
    return helikon_loader_run(memory_read, files, MANIFEST_PATH, IMAGE_PATH,
                              BOOTSTATE_PATH, HELIKON_BOARD_IPOD6G, 0,
                              helikon_key_table_verify, &table, image,
                              IMAGE_SIZE, result);
}

static void make_signed_files(struct memory_files *files)
{
    make_valid_files(files);
    memcpy(files->manifest + HELIKON_MANIFEST_SIGNED_SIZE,
           signed_manifest_signature, sizeof(signed_manifest_signature));
}

static int test_real_signature(void)
{
    struct memory_files files;
    make_signed_files(&files);
    uint8_t image[IMAGE_SIZE];
    struct helikon_loader_result result;
    CHECK(load_signed(&files, image, &result) == HELIKON_LOADER_OK);
    CHECK(result.manifest.image_version == 1);

    files.image[0] ^= 0xffu;
    CHECK(load_signed(&files, image, &result) == HELIKON_LOADER_ERR_HASH);

    /* A bad signature must be rejected before the image is read. */
    make_signed_files(&files);
    files.image_reads = 0;
    files.manifest[HELIKON_MANIFEST_SIGNED_SIZE] ^= 1u;
    CHECK(load_signed(&files, image, &result) ==
          HELIKON_LOADER_ERR_SIGNATURE);
    CHECK(files.image_reads == 0);

    make_signed_files(&files);
    files.manifest[17] ^= 1u; /* image version high byte, signed */
    CHECK(load_signed(&files, image, &result) ==
          HELIKON_LOADER_ERR_SIGNATURE);
    return 0;
}

static int test_valid_load(void)
{
    struct memory_files files;
    make_valid_files(&files);
    uint8_t image[IMAGE_SIZE];
    struct helikon_loader_result result;
    CHECK(load(&files, image, &result) == HELIKON_LOADER_OK);
    CHECK(result.image_size == IMAGE_SIZE);
    CHECK(result.bootstate_valid == 1);
    CHECK(result.bootstate.slot.image_version == 1);
    CHECK(memcmp(image, files.image, IMAGE_SIZE) == 0);
    return 0;
}

static int test_missing_bootstate_is_default(void)
{
    struct memory_files files;
    make_valid_files(&files);
    files.has_bootstate = 0;
    uint8_t image[IMAGE_SIZE];
    struct helikon_loader_result result;
    CHECK(load(&files, image, &result) == HELIKON_LOADER_OK);
    CHECK(result.bootstate_valid == 0);
    return 0;
}

static int test_failed_newest_falls_back(void)
{
    struct memory_files files;
    make_valid_files(&files);
    memset(files.bootstate, 0, sizeof(files.bootstate));
    make_slot(files.bootstate, 1, HELIKON_BOARD_IPOD6G, 1,
              HELIKON_DRAM_BASE, IMAGE_SIZE, files.image,
              HELIKON_BOOTSTATE_GOOD);
    make_slot(files.bootstate + HELIKON_BOOTSTATE_SLOT_SIZE, 2,
              HELIKON_BOARD_IPOD6G, 1, HELIKON_DRAM_BASE, IMAGE_SIZE,
              files.image, HELIKON_BOOTSTATE_FAILED);
    uint8_t image[IMAGE_SIZE];
    struct helikon_loader_result result;
    CHECK(load(&files, image, &result) == HELIKON_LOADER_OK);
    CHECK(result.bootstate_valid == 1);
    CHECK(result.bootstate.selected == 0);
    return 0;
}

static int test_bad_magic(void)
{
    struct memory_files files;
    make_valid_files(&files);
    files.manifest[0] = 'X';
    uint8_t image[IMAGE_SIZE];
    struct helikon_loader_result result;
    CHECK(load(&files, image, &result) == HELIKON_LOADER_ERR_MAGIC);
    CHECK(files.image_reads == 0);
    return 0;
}

static int test_bad_board(void)
{
    struct memory_files files;
    make_valid_files(&files);
    put32(files.manifest + 12, HELIKON_BOARD_IPOD7G);
    uint8_t image[IMAGE_SIZE];
    struct helikon_loader_result result;
    CHECK(load(&files, image, &result) == HELIKON_LOADER_ERR_BOARD);
    return 0;
}

static int test_bad_address(void)
{
    struct memory_files files;
    make_valid_files(&files);
    put32(files.manifest + 20, 0x01000000u);
    uint8_t image[IMAGE_SIZE];
    struct helikon_loader_result result;
    CHECK(load(&files, image, &result) == HELIKON_LOADER_ERR_ADDRESS);
    return 0;
}

static int test_hash_mismatch(void)
{
    struct memory_files files;
    make_valid_files(&files);
    files.image[0] ^= 0xffu;
    uint8_t image[IMAGE_SIZE];
    struct helikon_loader_result result;
    CHECK(load(&files, image, &result) == HELIKON_LOADER_ERR_HASH);
    return 0;
}

static int test_signature_rejected(void)
{
    struct memory_files files;
    make_valid_files(&files);
    files.reject_signature = 1;
    files.rejected_key = 7;
    uint8_t image[IMAGE_SIZE];
    struct helikon_loader_result result;
    CHECK(load(&files, image, &result) == HELIKON_LOADER_ERR_SIGNATURE);
    CHECK(files.image_reads == 0);
    return 0;
}

static int test_bootstate_manifest_mismatch(void)
{
    struct memory_files files;
    make_valid_files(&files);
    put32(files.bootstate + 16, 2); /* image version */
    helikon_sha256(files.bootstate, 64u, files.bootstate + 64);
    uint8_t image[IMAGE_SIZE];
    struct helikon_loader_result result;
    CHECK(load(&files, image, &result) == HELIKON_LOADER_ERR_BOOTSTATE);
    return 0;
}

static int test_truncated_bootstate_is_default(void)
{
    struct memory_files files;
    make_valid_files(&files);
    files.bootstate_size = 100;
    uint8_t image[IMAGE_SIZE];
    struct helikon_loader_result result;
    CHECK(load(&files, image, &result) == HELIKON_LOADER_OK);
    CHECK(result.bootstate_valid == 0);
    return 0;
}

static int test_corrupt_bootstate_is_default(void)
{
    struct memory_files files;
    make_valid_files(&files);
    files.bootstate[0] = 'X';
    uint8_t image[IMAGE_SIZE];
    struct helikon_loader_result result;
    CHECK(load(&files, image, &result) == HELIKON_LOADER_OK);
    CHECK(result.bootstate_valid == 0);
    return 0;
}

struct test_case {
    const char *name;
    int (*run)(void);
};

int main(void)
{
    static const struct test_case tests[] = {
        { "SHA-256 vectors", test_sha256_vectors },
        { "valid load", test_valid_load },
        { "real Ed25519 manifest", test_real_signature },
        { "missing bootstate default", test_missing_bootstate_is_default },
        { "failed newest fallback", test_failed_newest_falls_back },
        { "bad magic", test_bad_magic },
        { "bad board", test_bad_board },
        { "bad address", test_bad_address },
        { "hash mismatch", test_hash_mismatch },
        { "signature rejected", test_signature_rejected },
        { "bootstate mismatch", test_bootstate_manifest_mismatch },
        { "truncated bootstate default", test_truncated_bootstate_is_default },
        { "corrupt bootstate default", test_corrupt_bootstate_is_default }
    };
    int failures = 0;
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        int rc = tests[i].run();
        if (rc == 0) {
            printf("PASS %s\n", tests[i].name);
        } else {
            printf("FAIL %s\n", tests[i].name);
            ++failures;
        }
    }
    if (failures != 0) {
        fprintf(stderr, "%d test(s) failed\n", failures);
        return 1;
    }
    printf("%zu tests passed\n", sizeof(tests) / sizeof(tests[0]));
    return 0;
}
