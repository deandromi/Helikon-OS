/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Helikon-OS diagnostic bootloader hardware glue for the iPod Classic 6G.
 *
 * This file is compiled into a donor-derived Rockbox bootloader tree. It
 * replaces the firmware-loading path with a read-only HFS+ diagnostic:
 *
 *   splash -> partition scan -> Helikon HFS+ probe/catalog/read -> report
 *   -> wait for a button -> power off
 *
 * It never writes to the disk and never loads a firmware image. The ATA
 * callback is deliberately limited to reads.
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

#include "diagnostics.h"
#include "report.h"
#include "splash.h"

/* Donor partition-table scanner. It only reads; the HFS+ mount and overlay
 * engine are not linked into this image. */
#include "hfsplus_partition.h"

#define DIAG_SCRATCH_SIZE 32768u
#define DIAG_PARTITION_CAP 4
#define DIAG_PROBE_PATH "/rockbox.ipod"

static uint8_t catalog_scratch[DIAG_SCRATCH_SIZE]
    __attribute__((aligned(4)));
static uint8_t extents_scratch[DIAG_SCRATCH_SIZE]
    __attribute__((aligned(4)));

/* Rockbox mono bitmaps pack eight vertical pixels per byte. The Helikon
 * splash is row-major MSB-first, so convert it once before the LCD blit. */
static uint8_t splash_mono[HELIKON_SPLASH_BYTES]
    __attribute__((aligned(4)));

static int diag_read(void *context, uint64_t offset, void *buffer,
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

void helikon_splash_show(void)
{
    helikon_splash_pack_rockbox(splash_mono, sizeof(splash_mono));

    lcd_set_foreground(LCD_WHITE);
    lcd_set_background(LCD_BLACK);
    lcd_clear_display();
    lcd_mono_bitmap(splash_mono, 0, 0, HELIKON_SPLASH_WIDTH,
                    HELIKON_SPLASH_HEIGHT);
    lcd_update();
}

static void result_init(struct boot_diag_result *result)
{
    memset(result, 0, sizeof(*result));
    result->volume = BOOT_DIAG_NOT_TESTED;
    result->catalog = BOOT_DIAG_NOT_TESTED;
    result->read = BOOT_DIAG_NOT_TESTED;
    result->write = BOOT_DIAG_NOT_IMPLEMENTED;
    result->splash = BOOT_DIAG_PASS;
    result->journal = BOOT_DIAG_JOURNAL_UNKNOWN;
}

static void show_report(const struct boot_diag_result *result,
                        const struct hfs_partition *partition)
{
    char report[512];
    char detail[64];
    int y = 0;

    lcd_set_foreground(LCD_WHITE);
    lcd_set_background(LCD_BLACK);
    lcd_clear_display();

    if (boot_diag_format(result, report, sizeof(report)) < 0) {
        lcd_puts(0, y++, "REPORT FORMAT ERROR");
    } else {
        char *line = report;
        while (*line != '\0' && y < LCD_HEIGHT / SYSFONT_HEIGHT) {
            char *end = strchr(line, '\n');
            if (end != NULL)
                *end = '\0';
            lcd_puts(0, y++, line);
            if (end == NULL)
                break;
            line = end + 1;
        }
    }

    if (partition != NULL) {
        /* The bootloader formatter supports %lu, not long-long. */
        snprintf(detail, sizeof(detail), "Partition: %lu MiB",
                 (unsigned long)(partition->length >> 20));
        lcd_puts(0, y++, detail);
    }

    if (result->volume_error != HFS_OK) {
        snprintf(detail, sizeof(detail), "Volume error: %d",
                 result->volume_error);
        lcd_puts(0, y++, detail);
    } else if (result->catalog_error != HFS_OK) {
        snprintf(detail, sizeof(detail), "Catalog error: %d",
                 result->catalog_error);
        lcd_puts(0, y++, detail);
    } else if (result->read_error != HFS_OK) {
        snprintf(detail, sizeof(detail), "Read error: %d", result->read_error);
        lcd_puts(0, y++, detail);
    }

    y++;
    lcd_puts(0, y++, "Press any button to power off.");
    lcd_puts(0, y++, "Nothing was written to the disk.");
    lcd_update();
}

void helikon_diagnostic(void)
{
    struct boot_diag_result result;
    struct hfs_partition parts[DIAG_PARTITION_CAP];
    struct hfs_partition *best = NULL;
    struct storage_info info;
    uint64_t disk_size;
    int count;

    result_init(&result);

    /* Let the splash be visible while the disk is already spinning. */
    sleep(HZ * 3 / 2);

    if (button_hold()) {
        lcd_clear_display();
        lcd_puts(0, 0, "Hold switch is on.");
        lcd_puts(0, 1, "Unlock it and reboot.");
        lcd_update();
        sleep(HZ * 3);
        power_off();
        while (1)
            sleep(HZ);
    }

    storage_get_info(0, &info);
    disk_size = (uint64_t)info.num_sectors * info.sector_size;

    count = hfs_partition_scan(diag_read, NULL, disk_size, info.sector_size,
                               parts, DIAG_PARTITION_CAP);
    if (count > 0) {
        for (int i = 0; i < count; ++i) {
            if (parts[i].type != 0xaf)
                continue;
            if (best == NULL || parts[i].length > best->length)
                best = &parts[i];
        }
    }

    if (best == NULL) {
        result.volume = BOOT_DIAG_FAIL;
        result.volume_error = HFS_ERR_NOT_HFS;
    } else {
        int rc = boot_diag_run(diag_read, NULL, best->offset, best->length,
                               DIAG_PROBE_PATH, catalog_scratch,
                               sizeof(catalog_scratch), extents_scratch,
                               sizeof(extents_scratch), &result);
        if (rc != HFS_OK) {
            result.volume = BOOT_DIAG_FAIL;
            result.volume_error = rc;
        }
    }

    show_report(&result, best);

    /* Do not let a held button from the boot gesture skip the report. */
    sleep(HZ);
    while (button_status() != BUTTON_NONE)
        sleep(HZ / 20);
    while (button_status() == BUTTON_NONE)
        sleep(HZ / 20);

    if (power_input_status() != POWER_INPUT_NONE) {
        lcd_clear_display();
        lcd_puts(0, 0, "Unplug USB to power off.");
        lcd_update();
        while (power_input_status() != POWER_INPUT_NONE)
            sleep(HZ / 10);
    }

    lcd_clear_display();
    lcd_puts(0, 0, "Power off...");
    lcd_update();
    sleep(HZ / 2);

    power_off();
    while (1)
        sleep(HZ);
}
