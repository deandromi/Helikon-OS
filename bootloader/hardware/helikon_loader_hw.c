/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Helikon-OS firmware-loading bootloader hardware glue for the iPod Classic 6G.
 *
 * This file is compiled into a donor-derived Rockbox bootloader tree. It is a
 * separate DFU-only test image from the read-only diagnostic bootloader:
 *
 *   splash -> partition scan -> HFS+ probe -> read manifest
 *   -> verify signature -> read image directly to its load address
 *   -> verify SHA-256 -> select boot state -> execute signed test payload
 *
 * The image never writes to the disk. It only jumps after the signed manifest,
 * board, image version, load range, size and SHA-256 have been verified. This
 * test build executes a development-flagged payload that returns a marker; a
 * production firmware handoff is deliberately disabled.
 */
#include "config.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ata.h"
#include "button.h"
#include "common.h"
#include "font.h"
#include "kernel.h"
#include "lcd.h"
#include "power.h"
#include "storage.h"
#include "system.h"

#include "firmware_loader.h"
#include "hfs_loader.h"
#include "keys.h"
#include "loader_error.h"
#include "manifest.h"
#include "splash_color.h"

/* Donor partition-table scanner. It only reads. */
#include "hfsplus_partition.h"

#include "helikon_build_keys.h"

#define LOADER_SCRATCH_SIZE 32768u
#define LOADER_PARTITION_CAP 4
#define LOADER_MANIFEST_PATH "/.helikon/firmware.manifest"
#define LOADER_IMAGE_PATH "/.helikon/firmware.img"
#define LOADER_BOOTSTATE_PATH "/.helikon/bootstate"
#define LOADER_TEST_RETURN 0x4F4C4548u /* "HELO" little-endian */

/*
 * Temporary device bring-up aid: one short line at the bottom of the splash.
 * The final splash build should set this to 0 after the HFS+ path is proven.
 */
#define HELIKON_LOADER_PROGRESS 1

static uint8_t catalog_scratch[LOADER_SCRATCH_SIZE]
    __attribute__((aligned(4)));
static uint8_t extents_scratch[LOADER_SCRATCH_SIZE]
    __attribute__((aligned(4)));

static int loader_read(void *context, uint64_t offset, void *buffer,
                       size_t length)
{
    uint8_t *dest = buffer;
    uint8_t sector[512];
    (void)context;

    while (length > 0) {
        uint32_t in_sector = (uint32_t)(offset & 511u);
        uint64_t lba = offset >> 9;
        size_t chunk = 512u - in_sector;

        if (chunk > length)
            chunk = length;

        if (in_sector == 0 && chunk == sizeof(sector)) {
            if (ata_read_sectors(IF_MD(0,) (sector_t)lba, 1, dest) != 0)
                return -1;
            dest += sizeof(sector);
            offset += sizeof(sector);
            length -= sizeof(sector);
        } else {
            if (ata_read_sectors(IF_MD(0,) (sector_t)lba, 1, sector) != 0)
                return -1;
            memcpy(dest, sector + in_sector, chunk);
            dest += chunk;
            offset += chunk;
            length -= chunk;
        }
    }
    return 0;
}

static void show_lines(const char *const *lines, int count)
{
    lcd_set_foreground(LCD_WHITE);
    lcd_set_background(LCD_BLACK);
    lcd_clear_display();
    for (int i = 0; i < count; ++i)
        lcd_puts(0, i, lines[i]);
    lcd_update();
}

static void show_message(const char *line0, const char *line1)
{
    const char *lines[2] = { line0, line1 };
    show_lines(lines, 2);
}

#if HELIKON_LOADER_PROGRESS
static void progress(const char *text)
{
    char line[31];
    size_t length = strlen(text);
    if (length > 30u)
        length = 30u;
    memcpy(line, text, length);
    memset(line + length, ' ', 30u - length);
    line[30] = '\0';
    lcd_set_foreground(LCD_BLACK);
    lcd_set_background(0xf77cu); /* palette[0], exact splash off-white */
    lcd_set_drawmode(DRMODE_SOLID);
    lcd_puts(0, 28, line);
    lcd_update();
}
#else
#define progress(text) ((void)0)
#endif

static void power_off_after_button(void) __attribute__((noreturn));

static void power_off_after_button(void)
{
    /* Do not let a held button from the boot gesture skip the report. */
    sleep(HZ);
    while (button_status() != BUTTON_NONE)
        sleep(HZ / 20);
    while (button_status() == BUTTON_NONE)
        sleep(HZ / 20);

    if (power_input_status() != POWER_INPUT_NONE) {
        show_message("Unplug USB to power off.", "");
        while (power_input_status() != POWER_INPUT_NONE)
            sleep(HZ / 10);
    }

    show_message("Power off...", "");
    sleep(HZ / 2);

    power_off();
    while (1)
        sleep(HZ);
}

static void fail(const char *message, int error) __attribute__((noreturn));

static void fail(const char *message, int error)
{
    char code[48];
    if (error == HELIKON_LOADER_OK) {
        show_message(message, "");
    } else {
        snprintf(code, sizeof(code), "%s (%d)",
                 helikon_loader_strerror(error), error);
        show_message(message, code);
    }
    power_off_after_button();
}

static void fail_hfs(const char *message, int error) __attribute__((noreturn));

static void fail_hfs(const char *message, int error)
{
    char code[48];
    helikon_splash_set_hfs(0);
    sleep(HZ * 2);
    snprintf(code, sizeof(code), "HFS error: %d", error);
    show_message(message, code);
    power_off_after_button();
}

void helikon_loader_main(void)
{
    struct storage_info info;
    struct hfs_partition parts[LOADER_PARTITION_CAP];
    struct hfs_partition *best = NULL;
    uint64_t disk_size;
    int count;

    /* The donor main already painted the splash before storage init. */
    progress("storage ready");
    sleep(HZ / 2);

    if (button_hold()) {
        show_message("Hold switch is on.", "Unlock it and reboot.");
        sleep(HZ * 3);
        power_off();
        while (1)
            sleep(HZ);
    }

    progress("partition scan");
    storage_get_info(0, &info);
    disk_size = (uint64_t)info.num_sectors * info.sector_size;

    count = hfs_partition_scan(loader_read, NULL, disk_size, info.sector_size,
                               parts, LOADER_PARTITION_CAP);
    if (count > 0) {
        for (int i = 0; i < count; ++i) {
            if (parts[i].type != 0xaf)
                continue;
            if (best == NULL || parts[i].length > best->length)
                best = &parts[i];
        }
    }

    if (best == NULL)
        fail("No HFS+ partition found.", HELIKON_LOADER_OK);

    progress("HFS+ volume");
    struct hfs_volume volume;
    int rc = hfs_volume_probe(&volume, loader_read, NULL, best->offset,
                              best->length);
    if (rc != HFS_OK) {
        if (rc == HFS_ERR_DIRTY)
            fail_hfs("Journal is dirty; no replay.", rc);
        fail_hfs("HFS+ probe failed.", rc);
    }

    progress("HFS+ catalog");
    struct hfs_catalog catalog;
    rc = hfs_catalog_open(&catalog, &volume, catalog_scratch,
                          sizeof(catalog_scratch));
    if (rc != HFS_OK)
        fail_hfs("Catalog open failed.", rc);

    struct hfs_extents extents;
    int have_extents = hfs_extents_open(&extents, &volume, extents_scratch,
                                        sizeof(extents_scratch)) == HFS_OK;
    struct helikon_hfs_loader hfs_loader = {
        &catalog, have_extents ? &extents : NULL
    };

    /* Volume and catalog opened: the HFS+ read path is working. */
    helikon_splash_set_hfs(1);

    /*
     * Read the manifest once to learn the signed load address. The loader
     * verifies the signature again before reading any image byte, then reads
     * the image directly to that address.
     */
    progress("firmware manifest");
    uint8_t manifest_data[HELIKON_MANIFEST_SIZE];
    size_t done = 0;
    rc = helikon_hfs_read_file(&hfs_loader, LOADER_MANIFEST_PATH, 0,
                               manifest_data, sizeof(manifest_data), &done);
    if (rc != HELIKON_LOADER_OK)
        fail("Cannot read firmware manifest.", rc);

    struct helikon_manifest parsed;
    rc = helikon_manifest_parse(manifest_data, sizeof(manifest_data), &parsed);
    if (rc != HELIKON_LOADER_OK)
        fail("Invalid firmware manifest.", rc);

    if (parsed.image_size > HELIKON_DRAM_SIZE)
        fail("Firmware image too large.", HELIKON_LOADER_ERR_SIZE);

    progress("verify firmware");
    uint8_t *image = (uint8_t *)(uintptr_t)parsed.load_address;
    struct helikon_key_table keys = helikon_build_key_table;
    struct helikon_loader_result result;
    rc = helikon_loader_run(
        helikon_hfs_read_file, &hfs_loader, LOADER_MANIFEST_PATH,
        LOADER_IMAGE_PATH, LOADER_BOOTSTATE_PATH, HELIKON_BOARD_IPOD6G, 1,
        helikon_key_table_verify, &keys, image, parsed.image_size, &result);

    if (rc != HELIKON_LOADER_OK)
        fail("Firmware verification failed.", rc);

    /*
     * The manifest was read twice. A concurrent change between the two reads
     * must not make us execute a different address than the one we filled.
     */
    if (result.manifest.load_address != parsed.load_address ||
        result.manifest.image_size != parsed.image_size)
        fail("Manifest changed during boot.", HELIKON_LOADER_OK);

    char line[48];
    const char *lines[6];
    int n = 0;
    lines[n++] = "HELIKON-OS LOADER TEST";
    lines[n++] = "Firmware verified.";
    snprintf(line, sizeof(line), "Version: %lu",
             (unsigned long)result.manifest.image_version);
    lines[n++] = line;
    snprintf(line, sizeof(line), "Load: 0x%08lx",
             (unsigned long)result.manifest.load_address);
    lines[n++] = line;
    snprintf(line, sizeof(line), "Size: %lu bytes",
             (unsigned long)result.manifest.image_size);
    lines[n++] = line;
    lines[n++] = result.bootstate_valid ? "Boot state: GOOD"
                                        : "Boot state: DEFAULT";
    show_lines(lines, n);
    sleep(HZ * 2);

    if ((result.manifest.flags & HELIKON_MANIFEST_FLAG_DEVELOPMENT) == 0) {
        show_message("Production handoff is disabled",
                     "in this test image. Power off.");
        power_off_after_button();
    }

    show_message("Executing signed test payload...", "");
    sleep(HZ);

    /*
     * The image is already at its linked address in DRAM. Flush/invalidate
     * the caches before executing it. The test payload returns "HELO"; a real
     * firmware entry point would not return.
     */
    progress("jump");
    uint32_t (*entry)(void) =
        (uint32_t (*)(void))(uintptr_t)result.manifest.load_address;
    disable_irq();
    commit_discard_idcache();
    uint32_t marker = entry();
    enable_irq();

    if (marker == LOADER_TEST_RETURN) {
        show_message("JUMP OK", "Signed test payload executed.");
    } else {
        snprintf(line, sizeof(line), "Bad payload marker: 0x%08lx",
                 (unsigned long)marker);
        show_message("JUMP FAILED", line);
    }

    power_off_after_button();
}
