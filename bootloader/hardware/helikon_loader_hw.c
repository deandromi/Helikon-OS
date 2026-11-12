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

/*
 * Handoff marker, shared with firmware/boot/firmware_main.c. It sits in the
 * reserved boot-info page at 0x087ff000; the payload reads it before T-059
 * fills the page.
 */
#define HELIKON_HANDOFF_MARKER_ADDR 0x087ff800u
#define HELIKON_HANDOFF_MARKER 0x484c4f31u /* "HLO1" */

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
 * Temporary device bring-up aid. The final splash build sets this to 0; keep
 * the progress calls for a future instrumented build.
 */
#define HELIKON_LOADER_PROGRESS 0

static uint8_t catalog_scratch[LOADER_SCRATCH_SIZE]
    __attribute__((aligned(4)));
static uint8_t extents_scratch[LOADER_SCRATCH_SIZE]
    __attribute__((aligned(4)));

/*
 * The ATA logical sector size is not always 512. The 6G iFlash bridge can
 * report 4096-byte logical sectors, and the loader's old hard-coded 512-byte
 * arithmetic then read the APM at the wrong LBA and failed with "No HFS+
 * partition found". Keep one bounce sector sized for the largest supported
 * logical sector and use the live storage_info value.
 */
static uint32_t loader_sector_size = 512u;
static uint8_t loader_sector[4096] __attribute__((aligned(4)));

static uint32_t loader_sector_bytes(void)
{
    uint32_t size = loader_sector_size;
    if (size < 512u || size > sizeof(loader_sector) ||
        (size & (size - 1u)) != 0u)
        return 512u;
    return size;
}

static int loader_read(void *context, uint64_t offset, void *buffer,
                       size_t length)
{
    uint8_t *dest = buffer;
    uint32_t sector_size = loader_sector_bytes();
    (void)context;

    while (length > 0) {
        uint32_t in_sector = (uint32_t)(offset % sector_size);
        uint64_t lba = offset / sector_size;
        size_t chunk = sector_size - in_sector;

        if (chunk > length)
            chunk = length;

        /*
         * HFS+ B-tree nodes are several sectors. Read all complete sectors in
         * one ATA command when the destination is aligned and large enough;
         * an iFlash SD can otherwise spend minutes in one-sector command
         * overhead. A short read must use the bounce buffer, because the
         * caller's buffer may be exactly one 512-byte record.
         */
        if (in_sector == 0 && length >= sector_size &&
            ((uintptr_t)dest & 3u) == 0) {
            size_t sectors = length / sector_size;
            if (ata_read_sectors(IF_MD(0,) (sector_t)lba, (int)sectors,
                                 dest) != 0)
                return -1;
            size_t bytes = sectors * sector_size;
            dest += bytes;
            offset += bytes;
            length -= bytes;
        } else {
            if (ata_read_sectors(IF_MD(0,) (sector_t)lba, 1,
                                 loader_sector) != 0)
                return -1;
            memcpy(dest, loader_sector + in_sector, chunk);
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

    if (button_hold()) {
        show_message("Hold switch is on.", "Unlock it and reboot.");
        sleep(HZ * 3);
        power_off();
        while (1)
            sleep(HZ);
    }

    progress("partition scan");
    storage_get_info(0, &info);
    loader_sector_size = info.sector_size;
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

    if (best == NULL) {
        char detail[48];
        snprintf(detail, sizeof(detail), "scan %d sect %u", count,
                 (unsigned)info.sector_size);
        show_message("No HFS+ partition found.", detail);
        power_off_after_button();
    }

    progress("HFS+ volume");
    struct hfs_volume volume;
    int rc = hfs_volume_probe_readonly(&volume, loader_read, NULL,
                                       best->offset, best->length);
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

    if ((result.manifest.flags & HELIKON_MANIFEST_FLAG_DEVELOPMENT) == 0) {
        show_message("Production handoff is disabled",
                     "in this test image. Power off.");
        power_off_after_button();
    }

    /*
     * No confirmation screen and no fixed delay: the image is verified and in
     * DRAM, so jump immediately. The splash stays up while storage work runs;
     * the firmware's first frame replaces it.
     */

    /*
     * Stamp the handoff in the reserved boot-info page and pass a zero
     * boot-info pointer: the page is not filled yet (T-059).
     */
    *(volatile uint32_t *)HELIKON_HANDOFF_MARKER_ADDR =
        HELIKON_HANDOFF_MARKER;

    /*
     * The image is already at its linked address in DRAM. Flush/invalidate
     * the caches before executing it. The test payload returns "HELO"; a real
     * firmware entry point would not return.
     */
    progress("jump");
    uint32_t (*entry)(uintptr_t, uint32_t) =
        (uint32_t (*)(uintptr_t, uint32_t))(uintptr_t)
            result.manifest.load_address;
    disable_irq();
    commit_discard_idcache();
    uint32_t marker = entry(0u, 0u);
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
