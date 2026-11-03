/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "diagnostics.h"
#include "report.h"
#include "splash.h"
#include "test_check.h"
#include "test_fs_image.h"
#include "test_image.h"

#include <stdint.h>
#include <string.h>

#define DIAG_SCRATCH 512u

static int test_read_pass(void)
{
    struct boot_diag_result result;
    uint8_t catalog_scratch[DIAG_SCRATCH];
    uint8_t extents_scratch[DIAG_SCRATCH];

    test_fs_image_make();
    int rc = boot_diag_run(test_image_read, NULL, 0, TEST_IMAGE_SIZE,
                           "/.firmware/current.img", catalog_scratch,
                           sizeof(catalog_scratch), extents_scratch,
                           sizeof(extents_scratch), &result);
    CHECK(rc == HFS_OK);
    CHECK(result.volume == BOOT_DIAG_PASS);
    CHECK(result.catalog == BOOT_DIAG_PASS);
    CHECK(result.read == BOOT_DIAG_PASS);
    CHECK(result.write == BOOT_DIAG_NOT_IMPLEMENTED);
    CHECK(result.journal == BOOT_DIAG_JOURNAL_NOT_PRESENT);
    CHECK(result.bytes_read == 4);
    CHECK(result.block_size == 512);
    CHECK(result.blocks == 128);
    return 0;
}

static int test_missing_file_fails_read(void)
{
    struct boot_diag_result result;
    uint8_t catalog_scratch[DIAG_SCRATCH];
    uint8_t extents_scratch[DIAG_SCRATCH];

    test_fs_image_make();
    int rc = boot_diag_run(test_image_read, NULL, 0, TEST_IMAGE_SIZE,
                           "/.firmware/missing.img", catalog_scratch,
                           sizeof(catalog_scratch), extents_scratch,
                           sizeof(extents_scratch), &result);
    CHECK(rc == HFS_OK);
    CHECK(result.volume == BOOT_DIAG_PASS);
    CHECK(result.catalog == BOOT_DIAG_PASS);
    CHECK(result.read == BOOT_DIAG_FAIL);
    CHECK(result.read_error == HFS_ERR_NOT_FOUND);
    CHECK(result.write == BOOT_DIAG_NOT_IMPLEMENTED);
    return 0;
}

static int test_dirty_volume_fails_closed(void)
{
    struct boot_diag_result result;
    uint8_t catalog_scratch[DIAG_SCRATCH];
    uint8_t extents_scratch[DIAG_SCRATCH];

    test_fs_image_make();
    uint8_t *header = test_image + 1024;
    test_image_put_be32(header + 4, TEST_ATTR_UNMOUNTED | TEST_ATTR_INCONSISTENT);

    int rc = boot_diag_run(test_image_read, NULL, 0, TEST_IMAGE_SIZE,
                           "/.firmware/current.img", catalog_scratch,
                           sizeof(catalog_scratch), extents_scratch,
                           sizeof(extents_scratch), &result);
    CHECK(rc == HFS_OK);
    CHECK(result.volume == BOOT_DIAG_FAIL);
    CHECK(result.volume_error == HFS_ERR_DIRTY);
    CHECK(result.journal == BOOT_DIAG_JOURNAL_DIRTY);
    CHECK(result.catalog == BOOT_DIAG_NOT_TESTED);
    CHECK(result.read == BOOT_DIAG_NOT_TESTED);
    CHECK(result.write == BOOT_DIAG_NOT_IMPLEMENTED);
    return 0;
}

static int test_no_probe_path_skips_read(void)
{
    struct boot_diag_result result;
    uint8_t catalog_scratch[DIAG_SCRATCH];

    test_fs_image_make();
    int rc = boot_diag_run(test_image_read, NULL, 0, TEST_IMAGE_SIZE, NULL,
                           catalog_scratch, sizeof(catalog_scratch), NULL, 0,
                           &result);
    CHECK(rc == HFS_OK);
    CHECK(result.volume == BOOT_DIAG_PASS);
    CHECK(result.catalog == BOOT_DIAG_PASS);
    CHECK(result.read == BOOT_DIAG_NOT_TESTED);
    CHECK(result.write == BOOT_DIAG_NOT_IMPLEMENTED);
    return 0;
}


static int test_report_text(void)
{
    struct boot_diag_result result;
    uint8_t catalog_scratch[DIAG_SCRATCH];
    char report[512];

    test_fs_image_make();
    int rc = boot_diag_run(test_image_read, NULL, 0, TEST_IMAGE_SIZE,
                           "/.firmware/current.img", catalog_scratch,
                           sizeof(catalog_scratch), NULL, 0, &result);
    CHECK(rc == HFS_OK);
    rc = boot_diag_format(&result, report, sizeof(report));
    CHECK(rc > 0);
    CHECK(strstr(report, "HFS+ read: PASS") != NULL);
    CHECK(strstr(report, "HFS+ write: NOT IMPLEMENTED") != NULL);
    CHECK(strstr(report, "Splash: PASS") != NULL);
    return 0;
}

static int test_bad_arguments(void)
{
    struct boot_diag_result result;
    uint8_t catalog_scratch[DIAG_SCRATCH];

    CHECK(boot_diag_run(NULL, NULL, 0, TEST_IMAGE_SIZE, NULL, catalog_scratch,
                        sizeof(catalog_scratch), NULL, 0, &result) ==
          HFS_ERR_ARGUMENT);
    CHECK(boot_diag_run(test_image_read, NULL, 0, TEST_IMAGE_SIZE, NULL, NULL,
                        0, NULL, 0, &result) == HFS_ERR_ARGUMENT);
    CHECK(boot_diag_run(test_image_read, NULL, 0, TEST_IMAGE_SIZE, NULL,
                        catalog_scratch, sizeof(catalog_scratch), NULL, 0,
                        NULL) == HFS_ERR_ARGUMENT);
    return 0;
}

static int test_splash_asset(void)
{
    CHECK(HELIKON_SPLASH_WIDTH == 320);
    CHECK(HELIKON_SPLASH_HEIGHT == 240);
    CHECK(HELIKON_SPLASH_STRIDE == 40);
    size_t ink = 0;
    for (size_t i = 0; i < HELIKON_SPLASH_BYTES; ++i) {
        uint8_t value = helikon_splash_bits[i];
        while (value != 0) {
            ink += value & 1u;
            value >>= 1;
        }
    }
    CHECK(ink > 0);
    return 0;
}

struct test_case {
    const char *name;
    int (*run)(void);
};

int main(void)
{
    static const struct test_case tests[] = {
        {"read pass", test_read_pass},
        {"missing file fails read", test_missing_file_fails_read},
        {"dirty volume fails closed", test_dirty_volume_fails_closed},
        {"no probe path skips read", test_no_probe_path_skips_read},
        {"report text", test_report_text},
        {"bad arguments", test_bad_arguments},
        {"splash asset", test_splash_asset}
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
