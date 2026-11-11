/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <stddef.h>
#include "board.h"

#include "error.h"
#include "memory_map.h"

static const struct helikon_board board_6g = {
    .id = 6u,
    .name = "iPod Classic 6G",
    .image_base = HELIKON_IMAGE_BASE,
    .image_max = HELIKON_IMAGE_MAX,
    .boot_info_base = HELIKON_BOOT_INFO_BASE,
    .boot_info_size = HELIKON_BOOT_INFO_SIZE,
    .kernel_base = HELIKON_KERNEL_BASE,
    .kernel_size = HELIKON_KERNEL_SIZE
};

static int ata_read_op(void *context, uint64_t lba, uint32_t count,
                       void *buffer, uint32_t timeout_ms)
{
    return helikon_ata_read(context, lba, count, buffer, timeout_ms);
}

static int ata_write_op(void *context, uint64_t lba, uint32_t count,
                        const void *buffer, uint32_t timeout_ms)
{
    return helikon_ata_write(context, lba, count, buffer, timeout_ms);
}

static int ata_flush_op(void *context, uint32_t timeout_ms)
{
    return helikon_ata_flush(context, timeout_ms);
}

static int ata_power_op(void *context, int on, uint32_t timeout_ms)
{
    return helikon_ata_power(context, on, timeout_ms);
}

static const struct helikon_block_ops block_ops = {
    .read = ata_read_op,
    .write = ata_write_op,
    .flush = ata_flush_op,
    .power = ata_power_op
};

static const struct helikon_display_ops display_ops = {
    .present = helikon_lcd_present
};

void helikon_board_init(struct helikon_board_runtime *runtime)
{
    if (runtime == NULL)
        return;
    runtime->board = &board_6g;
    runtime->display = NULL;
    runtime->display_context = NULL;
}

int helikon_board_clock_init(struct helikon_board_runtime *runtime,
                             uint32_t timeout_ms)
{
    if (runtime == NULL)
        return HELIKON_ERR_ARGUMENT;
    return helikon_clock_init(&runtime->clock, timeout_ms);
}

int helikon_board_power_init(struct helikon_board_runtime *runtime,
                             uint32_t timeout_ms)
{
    if (runtime == NULL)
        return HELIKON_ERR_ARGUMENT;
    int rc = helikon_pmu_init(&runtime->pmu, &runtime->clock, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    return helikon_backlight_init(&runtime->backlight, &runtime->pmu,
                                  timeout_ms);
}

int helikon_board_display_init(struct helikon_board_runtime *runtime,
                               uint32_t timeout_ms)
{
    if (runtime == NULL)
        return HELIKON_ERR_ARGUMENT;
    int rc = helikon_lcd_init(&runtime->lcd, &runtime->clock, timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    runtime->display = &display_ops;
    runtime->display_context = &runtime->lcd;
    return HELIKON_OK;
}

int helikon_board_power_attach(struct helikon_board_runtime *runtime)
{
    if (runtime == NULL)
        return HELIKON_ERR_ARGUMENT;
    runtime->pmu.clock = &runtime->clock;
    runtime->pmu.initialized = 1;
    runtime->backlight.pmu = &runtime->pmu;
    runtime->backlight.initialized = 1;
    return HELIKON_OK;
}

int helikon_board_display_attach(struct helikon_board_runtime *runtime)
{
    if (runtime == NULL)
        return HELIKON_ERR_ARGUMENT;
    int rc = helikon_lcd_attach(&runtime->lcd, &runtime->clock);
    if (rc != HELIKON_OK)
        return rc;
    runtime->display = &display_ops;
    runtime->display_context = &runtime->lcd;
    return HELIKON_OK;
}

int helikon_board_storage_attach(struct helikon_board_runtime *runtime)
{
    if (runtime == NULL || !runtime->ata.initialized ||
        runtime->ata.sector_size < 512u ||
        runtime->ata.total_sectors == 0u)
        return HELIKON_ERR_ARGUMENT;

    struct helikon_block_info info = {
        .sector_size = runtime->ata.sector_size,
        .sector_count = runtime->ata.total_sectors,
        .flags = HELIKON_BLOCK_FLAG_READ_ONLY
    };
    return helikon_block_init(&runtime->block, &block_ops, &runtime->ata,
                              &info);
}

int helikon_board_storage_init(struct helikon_board_runtime *runtime,
                               uint32_t timeout_ms)
{
    if (runtime == NULL)
        return HELIKON_ERR_ARGUMENT;
    int rc = helikon_ata_init(&runtime->ata, &runtime->pmu, &runtime->clock,
                              timeout_ms);
    if (rc != HELIKON_OK)
        return rc;
    return helikon_board_storage_attach(runtime);
}

int helikon_board_input_init(struct helikon_board_runtime *runtime,
                             uint32_t timeout_ms)
{
    if (runtime == NULL)
        return HELIKON_ERR_ARGUMENT;
    return helikon_buttons_init(&runtime->buttons, &runtime->pmu,
                                &runtime->clock, timeout_ms);
}

int helikon_board_enable_write(struct helikon_board_runtime *runtime)
{
    if (runtime == NULL || runtime->block.ops == NULL)
        return HELIKON_ERR_ARGUMENT;
    runtime->block.info.flags &= ~HELIKON_BLOCK_FLAG_READ_ONLY;
    return HELIKON_OK;
}
