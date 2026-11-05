/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <stddef.h>
/*
 * Helikon-OS F1 read-only firmware skeleton.
 *
 * The entry sequence in boot/start.S calls firmware_main with the loader's
 * boot-info pointer. This file owns the visible bring-up ladder:
 *
 *   BOOT -> PMU -> LCD -> ATA -> PART -> HFS -> READ -> INPUT -> PASS
 *
 * Every stage is recorded in the RAM trace ring and on the panel. The firmware
 * opens the HFS+ volume read-only, reads /.helikon/firmware.manifest through
 * the existing HFS+ core, reports PASS and powers off. It never writes the
 * filesystem, never replays the journal and never loads unsigned code.
 */
#include "board.h"
#include "boot_info.h"
#include "bootstate.h"
#include "clock.h"
#include "cpu.h"
#include "error.h"
#include "format.h"
#include "hfs_block_adapter.h"
#include "hfs_catalog.h"
#include "hfs_catalog_compare.h"
#include "hfs_catalog_write.h"
#include "hfs_extents.h"
#include "hfs_file.h"
#include "hfs_journal.h"
#include "hfs_txn.h"
#include "hfs_volume.h"
#include "mem.h"
#include "panic.h"
#include "partition.h"
#include "screen.h"
#include "sha256.h"
#include "stack.h"
#include "stages.h"
#include "static_assert.h"
#include "string.h"
#include "trace.h"

#ifndef HELIKON_IMAGE_VERSION
#define HELIKON_IMAGE_VERSION 1u
#endif

#define BOOT_INFO_PATH "/.helikon/firmware.manifest"
#define BOOTSTATE_PATH "/.helikon/bootstate"
#define BOOTSTATE_NAME_UNITS 9u
#define BOOT_MANIFEST_SIZE 256u
#define BOOT_CATALOG_SCRATCH 32768u
#define BOOT_EXTENTS_SCRATCH 32768u
#define BOOT_BLOCK_SCRATCH 4096u
#define BOOT_PARTITION_CAP 4u
#define BOOT_CLOCK_TIMEOUT_MS 100u
#define BOOT_POWER_TIMEOUT_MS 2000u
#define BOOT_DISPLAY_TIMEOUT_MS 2000u
#define BOOT_STORAGE_TIMEOUT_MS 5000u
#define BOOT_READ_TIMEOUT_MS 3000u
#define BOOT_INPUT_TIMEOUT_MS 10000u
#define BOOT_INPUT_POLL_MS 20u

HELIKON_STATIC_ASSERT(HELIKON_SCREEN_WIDTH == HELIKON_LCD_WIDTH,
                      screen_width_matches_panel);
HELIKON_STATIC_ASSERT(HELIKON_SCREEN_HEIGHT == HELIKON_LCD_HEIGHT,
                      screen_height_matches_panel);

struct boot_context {
    struct helikon_board_runtime board;
    struct helikon_stages stages;
    struct helikon_trace trace;
    struct helikon_screen screen;
    struct helikon_panic_hooks panic_hooks;
    struct helikon_boot_info boot_info;
    struct helikon_partition partitions[BOOT_PARTITION_CAP];
    struct helikon_hfs_block_adapter adapter;
    struct hfs_volume volume;
    struct hfs_catalog catalog;
    struct hfs_extents extents;
    struct hfs_journal journal;
    struct hfs_txn txn;
    int boot_info_status;
    int have_extents;
    int screen_ready;
    uint32_t read_size;
    uint32_t bootstate_sequence;
    uint8_t bootstate_action; /* 0 none, 1 promoted, 2 already GOOD */
    uint8_t catalog_scratch[BOOT_CATALOG_SCRATCH] __attribute__((aligned(32)));
    uint8_t extents_scratch[BOOT_EXTENTS_SCRATCH] __attribute__((aligned(32)));
    uint8_t block_scratch[BOOT_BLOCK_SCRATCH] __attribute__((aligned(32)));
    uint8_t sector[512] __attribute__((aligned(32)));
    uint8_t manifest[BOOT_MANIFEST_SIZE] __attribute__((aligned(32)));
    uint8_t bootstate[HELIKON_BOOTSTATE_SIZE] __attribute__((aligned(32)));
    uint16_t framebuffer[HELIKON_SCREEN_WIDTH * HELIKON_SCREEN_HEIGHT]
        __attribute__((aligned(32)));
};

extern uint8_t __image_start[];
extern uint8_t __image_end[];

static struct boot_context boot;

static uint64_t stage_now(void *context)
{
    return helikon_clock_now_us((struct helikon_clock *)context);
}

static void text_append(char *buffer, size_t size, const char *text)
{
    size_t used = helikon_strlen(buffer, size);
    if (used < size)
        helikon_strlcpy(buffer + used, text, size - used);
}

static void append_u32(char *buffer, size_t size, uint32_t value)
{
    char number[16];
    helikon_format_u32(number, sizeof(number), value);
    text_append(buffer, size, number);
}

static void append_i32(char *buffer, size_t size, int32_t value)
{
    char number[16];
    helikon_format_i32(number, sizeof(number), value);
    text_append(buffer, size, number);
}

static const char *boot_info_label(int status)
{
    if (status == HELIKON_OK)
        return "OK";
    if (status == HELIKON_ERR_NOT_FOUND)
        return "MISSING";
    return "INVALID";
}

static const char *failure_reason(int error)
{
    switch (error) {
    case HELIKON_ERR_IO: return "Storage or device I/O failed.";
    case HELIKON_ERR_NOT_HFS: return "This volume is not native HFS+ v4.";
    case HELIKON_ERR_FORMAT: return "The filesystem structure did not validate.";
    case HELIKON_ERR_DIRTY: return "The journal is not clean; read-only.";
    case HELIKON_ERR_UNSUPPORTED: return "This object or feature is unsupported.";
    case HELIKON_ERR_NOT_FOUND: return "A required path was not found.";
    case HELIKON_ERR_BOOT_INFO_INVALID: return "The loader handoff did not validate.";
    case HELIKON_ERR_BOOTSTATE: return "The bootstate does not match this image.";
    case HELIKON_ERR_READ_ONLY: return "The storage write gate is closed.";
    case HELIKON_ERR_ARGUMENT: return "An internal argument check failed.";
    case HELIKON_ERR_BUFFER: return "A scratch buffer was too small.";
    default: return "An unexpected firmware error occurred.";
    }
}

static void paint_title(struct boot_context *ctx, const char *subtitle)
{
    helikon_screen_set_colors(&ctx->screen, HELIKON_COLOR_INK,
                              HELIKON_COLOR_BACKGROUND);
    helikon_screen_clear(&ctx->screen);
    helikon_screen_text(&ctx->screen, 16, 12, 4, "HELIKON-OS");
    helikon_screen_text(&ctx->screen, 16, 44, 2, subtitle);
}

static int present_or_trace(struct boot_context *ctx,
                            enum helikon_trace_level level, uint16_t event)
{
    int rc = helikon_screen_present_all(&ctx->screen);
    if (rc != HELIKON_OK) {
        helikon_trace_add(&ctx->trace, level, HELIKON_TRACE_DIAG, event,
                          (uint32_t)rc, 0u);
    }
    return rc;
}

static const char *bootstate_action_text(const struct boot_context *ctx)
{
    switch (ctx->bootstate_action) {
    case 1u: return "PROMOTED";
    case 2u: return "ALREADY GOOD";
    default: return "NOT RUN";
    }
}

static int paint_stages(struct boot_context *ctx)
{
    paint_title(ctx, "F2 BOOTSTATE PROMOTION");

    for (int stage = HELIKON_STAGE_BOOT; stage <= HELIKON_STAGE_WRITE;
         ++stage) {
        uint32_t y = 64u + (uint32_t)stage * 16u;
        const struct helikon_stage_record *record =
            helikon_stages_record(&ctx->stages, stage);

        helikon_screen_set_foreground(&ctx->screen, HELIKON_COLOR_INK);
        helikon_screen_text(&ctx->screen, 20, y, 2,
                            helikon_stage_code(stage));
        helikon_screen_text(&ctx->screen, 76, y, 2,
                            helikon_stage_text(stage));

        const char *state = "...";
        uint16_t color = HELIKON_COLOR_NEUTRAL;
        if (record != NULL && record->result == HELIKON_STAGE_PASSED) {
            state = "OK";
            color = HELIKON_COLOR_SUCCESS;
        } else if (record != NULL && record->result == HELIKON_STAGE_FAILED) {
            state = "FAIL";
            color = HELIKON_COLOR_FAILURE;
        }
        helikon_screen_set_foreground(&ctx->screen, color);
        helikon_screen_text(&ctx->screen, 258, y, 2, state);
    }

    char line[64] = "BOOT-INFO ";
    text_append(line, sizeof(line), boot_info_label(ctx->boot_info_status));
    text_append(line, sizeof(line), "  IMG ");
    if (ctx->boot_info_status == HELIKON_OK) {
        text_append(line, sizeof(line), "v");
        append_u32(line, sizeof(line), ctx->boot_info.image_version);
    } else {
        text_append(line, sizeof(line), "?");
    }
    helikon_screen_set_foreground(&ctx->screen, HELIKON_COLOR_NEUTRAL);
    helikon_screen_text(&ctx->screen, 20, 208, 2, line);

    char sizes[64] = "IMAGE ";
    append_u32(sizes, sizeof(sizes),
               (uint32_t)(__image_end - __image_start));
    text_append(sizes, sizeof(sizes), " B  BOARD ");
    append_u32(sizes, sizeof(sizes), ctx->board.board->id);
    helikon_screen_text(&ctx->screen, 20, 224, 2, sizes);

    return present_or_trace(ctx, HELIKON_TRACE_WARN, HELIKON_STAGE_LCD);
}

static int paint_pass(struct boot_context *ctx)
{
    paint_title(ctx, "F2 BOOTSTATE PROMOTION");

    helikon_screen_set_foreground(&ctx->screen, HELIKON_COLOR_SUCCESS);
    helikon_screen_text(&ctx->screen, 16, 62, 6, "PASS");

    char line[64];
    helikon_strlcpy(line, "IMG ", sizeof(line));
    append_u32(line, sizeof(line),
               (uint32_t)(__image_end - __image_start));
    text_append(line, sizeof(line), " B  STACK ");
    append_u32(line, sizeof(line), helikon_stack_used_bytes());
    text_append(line, sizeof(line), "/");
    append_u32(line, sizeof(line), helikon_stack_size_bytes());
    helikon_screen_set_foreground(&ctx->screen, HELIKON_COLOR_INK);
    helikon_screen_text(&ctx->screen, 16, 110, 2, line);

    helikon_strlcpy(line, "BOOT ", sizeof(line));
    append_u32(line, sizeof(line),
               (uint32_t)helikon_stages_elapsed_ms(&ctx->stages,
                                                   HELIKON_STAGE_BOOT));
    text_append(line, sizeof(line), " PMU ");
    append_u32(line, sizeof(line),
               (uint32_t)helikon_stages_elapsed_ms(&ctx->stages,
                                                   HELIKON_STAGE_PMU));
    text_append(line, sizeof(line), " LCD ");
    append_u32(line, sizeof(line),
               (uint32_t)helikon_stages_elapsed_ms(&ctx->stages,
                                                   HELIKON_STAGE_LCD));
    text_append(line, sizeof(line), " ATA ");
    append_u32(line, sizeof(line),
               (uint32_t)helikon_stages_elapsed_ms(&ctx->stages,
                                                   HELIKON_STAGE_ATA));
    helikon_screen_text(&ctx->screen, 16, 126, 2, line);

    helikon_strlcpy(line, "PART ", sizeof(line));
    append_u32(line, sizeof(line),
               (uint32_t)helikon_stages_elapsed_ms(&ctx->stages,
                                                   HELIKON_STAGE_PART));
    text_append(line, sizeof(line), " HFS ");
    append_u32(line, sizeof(line),
               (uint32_t)helikon_stages_elapsed_ms(&ctx->stages,
                                                   HELIKON_STAGE_HFS));
    text_append(line, sizeof(line), " READ ");
    append_u32(line, sizeof(line),
               (uint32_t)helikon_stages_elapsed_ms(&ctx->stages,
                                                   HELIKON_STAGE_READ));
    text_append(line, sizeof(line), " INPUT ");
    append_u32(line, sizeof(line),
               (uint32_t)helikon_stages_elapsed_ms(&ctx->stages,
                                                   HELIKON_STAGE_INPUT));
    helikon_screen_text(&ctx->screen, 16, 142, 2, line);

    helikon_strlcpy(line, "WRITE ", sizeof(line));
    append_u32(line, sizeof(line),
               (uint32_t)helikon_stages_elapsed_ms(&ctx->stages,
                                                   HELIKON_STAGE_WRITE));
    text_append(line, sizeof(line), "ms  ");
    text_append(line, sizeof(line), bootstate_action_text(ctx));
    helikon_screen_set_foreground(&ctx->screen,
                                  ctx->bootstate_action != 0u
                                      ? HELIKON_COLOR_SUCCESS
                                      : HELIKON_COLOR_NEUTRAL);
    helikon_screen_text(&ctx->screen, 16, 158, 2, line);

    helikon_strlcpy(line, "TRACE ", sizeof(line));
    append_u32(line, sizeof(line), (uint32_t)helikon_trace_count(&ctx->trace));
    text_append(line, sizeof(line), " DROPPED ");
    append_u32(line, sizeof(line), helikon_trace_dropped(&ctx->trace));
    helikon_screen_set_foreground(&ctx->screen, HELIKON_COLOR_INK);
    helikon_screen_text(&ctx->screen, 16, 174, 2, line);

    helikon_strlcpy(line, "BOOT-INFO ", sizeof(line));
    text_append(line, sizeof(line), boot_info_label(ctx->boot_info_status));
    helikon_screen_set_foreground(&ctx->screen, HELIKON_COLOR_NEUTRAL);
    helikon_screen_text(&ctx->screen, 16, 190, 2, line);

    helikon_strlcpy(line, "BOOTSTATE ", sizeof(line));
    text_append(line, sizeof(line),
                ctx->bootstate_action != 0u ? "GOOD" : "?");
    text_append(line, sizeof(line), " SEQ ");
    append_u32(line, sizeof(line), ctx->bootstate_sequence);
    helikon_screen_set_foreground(&ctx->screen,
                                  ctx->bootstate_action != 0u
                                      ? HELIKON_COLOR_SUCCESS
                                      : HELIKON_COLOR_NEUTRAL);
    helikon_screen_text(&ctx->screen, 16, 206, 2, line);

    if (ctx->read_size != 0u) {
        helikon_strlcpy(line, "READ ", sizeof(line));
        append_u32(line, sizeof(line), ctx->read_size);
        text_append(line, sizeof(line), " B  firmware.manifest");
        helikon_screen_set_foreground(&ctx->screen, HELIKON_COLOR_INK);
        helikon_screen_text(&ctx->screen, 16, 222, 2, line);
    }

    return present_or_trace(ctx, HELIKON_TRACE_ERROR, HELIKON_STAGE_PASS);
}

static void paint_failure(struct boot_context *ctx, int stage, int error,
                          const char *reason)
{
    paint_title(ctx, "F1 READ-ONLY SKELETON");

    helikon_screen_set_foreground(&ctx->screen, HELIKON_COLOR_FAILURE);
    helikon_screen_text(&ctx->screen, 16, 62, 6, "FAIL");

    char line[64] = "STAGE ";
    text_append(line, sizeof(line), helikon_stage_code(stage));
    text_append(line, sizeof(line), "  ERROR ");
    append_i32(line, sizeof(line), error);
    helikon_screen_set_foreground(&ctx->screen, HELIKON_COLOR_INK);
    helikon_screen_text(&ctx->screen, 16, 112, 2, line);

    helikon_screen_text(&ctx->screen, 16, 132, 2, reason);
    helikon_screen_set_foreground(&ctx->screen, HELIKON_COLOR_NEUTRAL);
    helikon_screen_text(&ctx->screen, 16, 168, 2,
                        "No filesystem write was attempted.");
    helikon_screen_text(&ctx->screen, 16, 188, 2, "Powering off.");
    present_or_trace(ctx, HELIKON_TRACE_ERROR, HELIKON_STAGE_FAIL);
}

static void paint_message(struct boot_context *ctx, const char *line0,
                          const char *line1)
{
    paint_title(ctx, "F1 READ-ONLY SKELETON");
    helikon_screen_set_foreground(&ctx->screen, HELIKON_COLOR_INK);
    helikon_screen_text(&ctx->screen, 16, 100, 3, line0);
    if (line1 != NULL)
        helikon_screen_text(&ctx->screen, 16, 136, 2, line1);
    present_or_trace(ctx, HELIKON_TRACE_WARN, HELIKON_STAGE_FAIL);
}

static void panic_disable(void *context)
{
    (void)context;
    helikon_cpu_disable_interrupts();
}

static void panic_paint(const struct helikon_crash_record *record,
                        void *context)
{
    struct boot_context *ctx = context;
    if (ctx == NULL || !ctx->screen_ready)
        return;

    paint_title(ctx, "FIRMWARE STOPPED");
    helikon_screen_set_foreground(&ctx->screen, HELIKON_COLOR_FAILURE);
    helikon_screen_text(&ctx->screen, 16, 62, 6, "PANIC");

    char line[64] = "REASON ";
    append_u32(line, sizeof(line), record->reason);
    text_append(line, sizeof(line), "  STAGE ");
    if (record->stage >= 0 && record->stage < HELIKON_STAGE_COUNT)
        text_append(line, sizeof(line),
                    helikon_stage_code(record->stage));
    else
        text_append(line, sizeof(line), "?");
    helikon_screen_set_foreground(&ctx->screen, HELIKON_COLOR_INK);
    helikon_screen_text(&ctx->screen, 16, 112, 2, line);

    helikon_strlcpy(line, "LR 0x", sizeof(line));
    char address[16];
    helikon_format_hex32(address, sizeof(address), record->lr, 8u);
    text_append(line, sizeof(line), address);
    helikon_screen_text(&ctx->screen, 16, 132, 2, line);
    helikon_screen_set_foreground(&ctx->screen, HELIKON_COLOR_NEUTRAL);
    helikon_screen_text(&ctx->screen, 16, 168, 2,
                        "No filesystem write was attempted.");
    present_or_trace(ctx, HELIKON_TRACE_ERROR, HELIKON_PANIC_ASSERT);
}

static void panic_power_off(void *context)
{
    struct boot_context *ctx = context;
    if (ctx != NULL && ctx->board.pmu.initialized)
        helikon_pmu_power_off(&ctx->board.pmu, 1000u);
    helikon_cpu_disable_interrupts();
    for (;;)
        helikon_cpu_idle();
}

static void stage_begin(struct boot_context *ctx, int stage)
{
    int rc = helikon_stages_begin(&ctx->stages, stage);
    if (rc != HELIKON_OK)
        helikon_panic(HELIKON_PANIC_INVARIANT, 0u, (uint32_t)stage);
    helikon_panic_set_stage(stage);
    helikon_trace_add(&ctx->trace, HELIKON_TRACE_INFO, HELIKON_TRACE_BOOT,
                      (uint16_t)stage, 0u, 0u);
}

static void stage_pass(struct boot_context *ctx, int stage)
{
    int rc = helikon_stages_pass(&ctx->stages, stage);
    if (rc != HELIKON_OK)
        helikon_panic(HELIKON_PANIC_INVARIANT, 0u, (uint32_t)stage);
}

static void boot_power_off(struct boot_context *ctx) __attribute__((noreturn));

static void boot_fail(struct boot_context *ctx, int stage, int error,
                      const char *reason) __attribute__((noreturn));

static void paint_stages_checked(struct boot_context *ctx, int stage)
{
    int rc = paint_stages(ctx);
    if (rc != HELIKON_OK)
        boot_fail(ctx, stage, rc, "The display transfer failed.");
}

static void boot_power_off(struct boot_context *ctx)
{
    if (ctx->board.pmu.initialized) {
        int external = 0;
        int rc = helikon_pmu_external_power(&ctx->board.pmu, &external,
                                            1000u);
        if (rc == HELIKON_OK && external) {
            if (ctx->screen_ready)
                paint_message(ctx, "UNPLUG USB", "to power off.");
            for (unsigned int i = 0; i < 300u; ++i) {
                helikon_clock_delay_us(&ctx->board.clock, 100000u);
                if (helikon_pmu_external_power(&ctx->board.pmu, &external,
                                               1000u) != HELIKON_OK ||
                    !external)
                    break;
            }
        }
        if (ctx->screen_ready)
            paint_message(ctx, "POWER OFF", NULL);
        helikon_clock_delay_us(&ctx->board.clock, 500000u);
        helikon_pmu_power_off(&ctx->board.pmu, 1000u);
    }

    helikon_cpu_disable_interrupts();
    for (;;)
        helikon_cpu_idle();
}

static void boot_fail(struct boot_context *ctx, int stage, int error,
                      const char *reason) __attribute__((noreturn));

static void boot_fail(struct boot_context *ctx, int stage, int error,
                      const char *reason)
{
    (void)helikon_stages_fail(&ctx->stages, stage, error);
    helikon_trace_add(&ctx->trace, HELIKON_TRACE_ERROR, HELIKON_TRACE_BOOT,
                      (uint16_t)stage, (uint32_t)error, 0u);
    helikon_panic_set_stage(HELIKON_STAGE_FAIL);
    if (ctx->screen_ready)
        paint_failure(ctx, stage, error, reason);
    if (ctx->board.clock.initialized)
        helikon_clock_delay_us(&ctx->board.clock, 3000000u);
    boot_power_off(ctx);
}

static void select_partition(struct boot_context *ctx)
{
    const struct helikon_partition *best = NULL;
    unsigned int count = 0;
    int rc = helikon_partition_scan(&ctx->board.block, ctx->partitions,
                                    BOOT_PARTITION_CAP, ctx->block_scratch,
                                    sizeof(ctx->block_scratch),
                                    BOOT_READ_TIMEOUT_MS, &count);
    if (rc < 0)
        boot_fail(ctx, HELIKON_STAGE_PART, rc, failure_reason(rc));
    if (count == 0u)
        boot_fail(ctx, HELIKON_STAGE_PART, HELIKON_ERR_NOT_HFS,
                  "No HFS+ volume was found.");

    for (unsigned int i = 0; i < count; ++i) {
        if (ctx->partitions[i].type != 0xafu)
            continue;
        if (best == NULL || ctx->partitions[i].length > best->length)
            best = &ctx->partitions[i];
    }
    if (best == NULL)
        boot_fail(ctx, HELIKON_STAGE_PART, HELIKON_ERR_NOT_HFS,
                  "No HFS+ volume was found.");
    ctx->partitions[0] = *best;
}

static int promote_bootstate(struct boot_context *ctx)
{
    struct hfs_entry entry;
    int rc = hfs_catalog_lookup(&ctx->catalog, BOOTSTATE_PATH, &entry);
    if (rc != HELIKON_OK)
        return rc == HELIKON_ERR_NOT_FOUND ? HELIKON_ERR_BOOTSTATE : rc;
    if (entry.kind != 2u || entry.unsupported != 0u ||
        entry.data.size != HELIKON_BOOTSTATE_SIZE)
        return HELIKON_ERR_BOOTSTATE;

    size_t done = 0;
    rc = helikon_hfs_read_file(&ctx->catalog,
                               ctx->have_extents ? &ctx->extents : NULL,
                               BOOTSTATE_PATH, 0u, ctx->bootstate,
                               sizeof(ctx->bootstate), &done);
    if (rc != HELIKON_OK)
        return rc == HELIKON_ERR_NOT_FOUND ? HELIKON_ERR_BOOTSTATE : rc;
    if (done != sizeof(ctx->bootstate))
        return HELIKON_ERR_BOOTSTATE;

    uint8_t digest[HELIKON_SHA256_SIZE];
    uint32_t image_size = (uint32_t)(__image_end - __image_start);
    helikon_sha256(__image_start, image_size, digest);

    uint32_t sequence = 0;
    int changed = 0;
    rc = helikon_bootstate_promote(ctx->bootstate, sizeof(ctx->bootstate),
                                   ctx->board.board->id,
                                   HELIKON_IMAGE_VERSION,
                                   ctx->board.board->image_base,
                                   image_size, digest, &sequence, &changed);
    if (rc != HELIKON_OK)
        return rc;
    ctx->bootstate_sequence = sequence;
    if (!changed) {
        ctx->bootstate_action = 2u;
        return HELIKON_OK;
    }

    /*
     * The bootstate write is the first firmware write. Re-check the journal
     * cleanly before opening the write gate; a dirty volume stays read-only.
     */
    struct hfs_journal probe;
    rc = hfs_journal_open(&probe, &ctx->volume, NULL, NULL);
    if (rc != HELIKON_OK)
        return rc;
    if (!hfs_journal_is_clean(&probe))
        return HELIKON_ERR_DIRTY;

    rc = helikon_board_enable_write(&ctx->board);
    if (rc != HELIKON_OK)
        return rc;
    rc = hfs_journal_open(&ctx->journal, &ctx->volume,
                          helikon_hfs_block_write, helikon_hfs_block_flush);
    if (rc != HELIKON_OK)
        return rc;

    rc = hfs_txn_begin(&ctx->txn, &ctx->journal);
    if (rc != HELIKON_OK)
        return rc;

    static const uint8_t name[BOOTSTATE_NAME_UNITS * 2u] = {
        0, 'b', 0, 'o', 0, 'o', 0, 't', 0, 's', 0, 't', 0, 'a', 0, 't',
        0, 'e'
    };
    rc = hfs_catalog_overwrite_file(&ctx->catalog, &ctx->txn, entry.parent,
                                    name, BOOTSTATE_NAME_UNITS, 0u,
                                    ctx->bootstate, sizeof(ctx->bootstate),
                                    hfs_catalog_key_compare, NULL);
    if (rc != HELIKON_OK) {
        hfs_txn_abort(&ctx->txn);
        return rc;
    }

    rc = hfs_txn_commit(&ctx->txn);
    if (rc != HELIKON_OK)
        return rc;

    uint8_t readback[HELIKON_BOOTSTATE_SIZE];
    done = 0;
    rc = helikon_hfs_read_file(&ctx->catalog,
                               ctx->have_extents ? &ctx->extents : NULL,
                               BOOTSTATE_PATH, 0u, readback,
                               sizeof(readback), &done);
    if (rc != HELIKON_OK)
        return rc;
    if (done != sizeof(readback) ||
        memcmp(readback, ctx->bootstate, sizeof(readback)) != 0)
        return HELIKON_ERR_BOOTSTATE;

    uint32_t readback_sequence = 0;
    int readback_changed = 0;
    rc = helikon_bootstate_promote(readback, sizeof(readback),
                                   ctx->board.board->id,
                                   HELIKON_IMAGE_VERSION,
                                   ctx->board.board->image_base,
                                   image_size, digest, &readback_sequence,
                                   &readback_changed);
    if (rc != HELIKON_OK || readback_changed != 0 ||
        readback_sequence != ctx->bootstate_sequence)
        return HELIKON_ERR_BOOTSTATE;

    ctx->bootstate_action = 1u;
    return HELIKON_OK;
}

void firmware_main(uintptr_t boot_info_pointer, uint32_t reserved)
{
    (void)reserved;
    struct boot_context *ctx = &boot;

    helikon_board_init(&ctx->board);
    helikon_trace_init(&ctx->trace, stage_now, &ctx->board.clock);
    helikon_stages_init(&ctx->stages, stage_now, &ctx->board.clock);

    ctx->panic_hooks.disable_interrupts = panic_disable;
    ctx->panic_hooks.paint = panic_paint;
    ctx->panic_hooks.power_off = panic_power_off;
    ctx->panic_hooks.context = ctx;
    helikon_panic_set_hooks(&ctx->panic_hooks);
    helikon_panic_set_trace(&ctx->trace);

    stage_begin(ctx, HELIKON_STAGE_BOOT);

    const struct helikon_board *board = ctx->board.board;
    struct helikon_boot_info_expect expect = {
        .page_base = (uintptr_t)board->boot_info_base,
        .page_size = board->boot_info_size,
        .board_id = board->id,
        .image_load = board->image_base,
        .image_size = (uint32_t)(__image_end - __image_start)
    };
    ctx->boot_info_status = helikon_boot_info_parse(boot_info_pointer, &expect,
                                                    &ctx->boot_info);
    helikon_trace_add(&ctx->trace, HELIKON_TRACE_INFO, HELIKON_TRACE_BOOT,
                      0x1000u, (uint32_t)ctx->boot_info_status,
                      (uint32_t)boot_info_pointer);
    stage_pass(ctx, HELIKON_STAGE_BOOT);

    stage_begin(ctx, HELIKON_STAGE_PMU);
    int rc = helikon_board_clock_init(&ctx->board, BOOT_CLOCK_TIMEOUT_MS);
    if (rc != HELIKON_OK)
        boot_fail(ctx, HELIKON_STAGE_PMU, rc, "The clock did not start.");
    helikon_trace_add(&ctx->trace, HELIKON_TRACE_INFO, HELIKON_TRACE_HAL,
                      0x2000u, (uint32_t)helikon_cpu_control(), 0u);

#ifdef HELIKON_DIAG_EARLY_DISPLAY
    /*
     * Diagnostic image only: prove the F2 entry and LCD path before touching
     * PMU/backlight. If this message appears, the loader jump, F2 _start, BSS
     * and LCD init are alive; a later black screen is then PMU/backlight or a
     * subsequent stage.
     */
    rc = helikon_board_display_init(&ctx->board, BOOT_DISPLAY_TIMEOUT_MS);
    if (rc != HELIKON_OK)
        boot_fail(ctx, HELIKON_STAGE_PMU, rc, "Early display init failed.");
    helikon_screen_init(&ctx->screen, ctx->framebuffer,
                        HELIKON_SCREEN_WIDTH, HELIKON_SCREEN_HEIGHT,
                        ctx->board.display, ctx->board.display_context);
    ctx->screen_ready = 1;
    paint_title(ctx, "EARLY DISPLAY OK");
    helikon_screen_set_foreground(&ctx->screen, HELIKON_COLOR_INK);
    helikon_screen_text(&ctx->screen, 16, 80, 2, "F2 CODE RUNNING");
    helikon_screen_text(&ctx->screen, 16, 100, 2, "BEFORE PMU INIT");
    if (present_or_trace(ctx, HELIKON_TRACE_ERROR, HELIKON_STAGE_PMU) !=
        HELIKON_OK)
        boot_fail(ctx, HELIKON_STAGE_PMU, HELIKON_ERR_IO,
                  "Early display transfer failed.");
#endif

    rc = helikon_board_power_init(&ctx->board, BOOT_POWER_TIMEOUT_MS);
    if (rc != HELIKON_OK)
        boot_fail(ctx, HELIKON_STAGE_PMU, rc,
                  "PMU or backlight initialization failed.");
#ifdef HELIKON_DIAG_EARLY_DISPLAY
    paint_message(ctx, "PMU OK", "DIAGNOSTIC COMPLETE");
    helikon_clock_delay_us(&ctx->board.clock, 3000000u);
    boot_power_off(ctx);
#endif
    stage_pass(ctx, HELIKON_STAGE_PMU);

    stage_begin(ctx, HELIKON_STAGE_LCD);
    rc = helikon_board_display_init(&ctx->board, BOOT_DISPLAY_TIMEOUT_MS);
    if (rc != HELIKON_OK)
        boot_fail(ctx, HELIKON_STAGE_LCD, rc, "The panel did not initialize.");
    helikon_screen_init(&ctx->screen, ctx->framebuffer,
                        HELIKON_SCREEN_WIDTH, HELIKON_SCREEN_HEIGHT,
                        ctx->board.display, ctx->board.display_context);
    ctx->screen_ready = 1;
    helikon_trace_add(&ctx->trace, HELIKON_TRACE_INFO, HELIKON_TRACE_HAL,
                      0x3000u, ctx->board.lcd.panel_type,
                      ctx->board.lcd.cmdset);
    paint_stages_checked(ctx, HELIKON_STAGE_LCD);
    stage_pass(ctx, HELIKON_STAGE_LCD);

    stage_begin(ctx, HELIKON_STAGE_ATA);
    rc = helikon_board_storage_init(&ctx->board, BOOT_STORAGE_TIMEOUT_MS);
    if (rc != HELIKON_OK)
        boot_fail(ctx, HELIKON_STAGE_ATA, rc, "Storage was not found.");
    rc = helikon_block_read(&ctx->board.block, 0u, 1u, ctx->sector,
                            BOOT_READ_TIMEOUT_MS);
    if (rc != HELIKON_OK)
        boot_fail(ctx, HELIKON_STAGE_ATA, rc, "A storage read failed.");
    helikon_trace_add(&ctx->trace, HELIKON_TRACE_INFO, HELIKON_TRACE_BLOCK,
                      0x4000u, (uint32_t)ctx->board.block.info.sector_count,
                      ctx->board.block.info.sector_size);
    stage_pass(ctx, HELIKON_STAGE_ATA);
    paint_stages_checked(ctx, HELIKON_STAGE_ATA);

    stage_begin(ctx, HELIKON_STAGE_PART);
    select_partition(ctx);
    helikon_trace_add(&ctx->trace, HELIKON_TRACE_INFO, HELIKON_TRACE_BLOCK,
                      0x5000u, (uint32_t)ctx->partitions[0].offset,
                      (uint32_t)(ctx->partitions[0].length >> 20));
    stage_pass(ctx, HELIKON_STAGE_PART);
    paint_stages_checked(ctx, HELIKON_STAGE_PART);

    stage_begin(ctx, HELIKON_STAGE_HFS);
    rc = helikon_hfs_block_adapter_init(&ctx->adapter, &ctx->board.block,
                                        ctx->block_scratch,
                                        sizeof(ctx->block_scratch),
                                        BOOT_READ_TIMEOUT_MS);
    if (rc != HELIKON_OK)
        boot_fail(ctx, HELIKON_STAGE_HFS, rc,
                  "The HFS+ read adapter did not initialize.");
    rc = hfs_volume_probe(&ctx->volume, helikon_hfs_block_read, &ctx->adapter,
                          ctx->partitions[0].offset,
                          ctx->partitions[0].length);
    if (rc != HELIKON_OK)
        boot_fail(ctx, HELIKON_STAGE_HFS, rc,
                  rc == HELIKON_ERR_DIRTY
                      ? "The journal is not clean; mounted read-only."
                      : failure_reason(rc));
    rc = hfs_catalog_open(&ctx->catalog, &ctx->volume, ctx->catalog_scratch,
                          sizeof(ctx->catalog_scratch));
    if (rc != HELIKON_OK)
        boot_fail(ctx, HELIKON_STAGE_HFS, rc, "The catalog did not open.");
    ctx->have_extents =
        hfs_extents_open(&ctx->extents, &ctx->volume, ctx->extents_scratch,
                         sizeof(ctx->extents_scratch)) == HELIKON_OK;
    helikon_trace_add(&ctx->trace, HELIKON_TRACE_INFO, HELIKON_TRACE_HFS,
                      0x6000u, ctx->volume.block_size, ctx->volume.blocks);
    stage_pass(ctx, HELIKON_STAGE_HFS);
    paint_stages_checked(ctx, HELIKON_STAGE_HFS);

    stage_begin(ctx, HELIKON_STAGE_READ);
    size_t done = 0;
    rc = helikon_hfs_read_file(&ctx->catalog,
                               ctx->have_extents ? &ctx->extents : NULL,
                               BOOT_INFO_PATH, 0u, ctx->manifest,
                               sizeof(ctx->manifest), &done);
    if (rc != HELIKON_OK)
        boot_fail(ctx, HELIKON_STAGE_READ, rc,
                  rc == HELIKON_ERR_NOT_FOUND
                      ? "The firmware manifest was not found."
                      : failure_reason(rc));
    uint32_t first_bytes = (uint32_t)ctx->manifest[0] |
                           ((uint32_t)ctx->manifest[1] << 8) |
                           ((uint32_t)ctx->manifest[2] << 16) |
                           ((uint32_t)ctx->manifest[3] << 24);
    helikon_trace_add(&ctx->trace, HELIKON_TRACE_INFO, HELIKON_TRACE_HFS,
                      0x7000u, (uint32_t)done, first_bytes);
    ctx->read_size = (uint32_t)done;
    stage_pass(ctx, HELIKON_STAGE_READ);
    paint_stages_checked(ctx, HELIKON_STAGE_READ);

    stage_begin(ctx, HELIKON_STAGE_INPUT);
    rc = helikon_board_input_init(&ctx->board, BOOT_POWER_TIMEOUT_MS);
    if (rc != HELIKON_OK)
        boot_fail(ctx, HELIKON_STAGE_INPUT, rc,
                  "The click wheel did not initialize.");
    paint_message(ctx, "PRESS ANY BUTTON", "to continue the self-test.");

    int hold_seen = 0;
    int input_seen = 0;
    uint64_t input_start = helikon_clock_now_us(&ctx->board.clock);
    while (!input_seen) {
        uint32_t events = 0;
        int32_t wheel_delta = 0;
        int hold = 0;
        rc = helikon_buttons_poll(&ctx->board.buttons, &events, &wheel_delta,
                                  &hold, BOOT_READ_TIMEOUT_MS);
        if (rc == HELIKON_OK) {
            if (hold) {
                hold_seen = 1;
            } else {
                if (hold_seen) {
                    hold_seen = 0;
                    paint_message(ctx, "PRESS ANY BUTTON",
                                  "to continue the self-test.");
                }
                if ((events & HELIKON_BUTTON_ANY) != 0u ||
                    wheel_delta != 0) {
                    input_seen = 1;
                    helikon_trace_add(&ctx->trace, HELIKON_TRACE_INFO,
                                      HELIKON_TRACE_HAL, 0x8000u, events,
                                      (uint32_t)wheel_delta);
                }
            }
        }
        if (!input_seen &&
            helikon_clock_expired(&ctx->board.clock, input_start,
                                  BOOT_INPUT_TIMEOUT_MS)) {
            if (hold_seen)
                boot_fail(ctx, HELIKON_STAGE_INPUT, HELIKON_ERR_IO,
                          "The hold switch is still on.");
            boot_fail(ctx, HELIKON_STAGE_INPUT, HELIKON_ERR_IO,
                      "No input event was observed.");
        }
        if (!input_seen)
            helikon_clock_delay_us(&ctx->board.clock,
                                   BOOT_INPUT_POLL_MS * 1000u);
    }
    stage_pass(ctx, HELIKON_STAGE_INPUT);
    paint_stages_checked(ctx, HELIKON_STAGE_INPUT);

    stage_begin(ctx, HELIKON_STAGE_WRITE);
    rc = promote_bootstate(ctx);
    if (rc != HELIKON_OK)
        boot_fail(ctx, HELIKON_STAGE_WRITE, rc,
                  rc == HELIKON_ERR_DIRTY
                      ? "The journal is not clean; no write was attempted."
                      : failure_reason(rc));
    stage_pass(ctx, HELIKON_STAGE_WRITE);
    paint_stages_checked(ctx, HELIKON_STAGE_WRITE);

    stage_begin(ctx, HELIKON_STAGE_PASS);
    if (!helikon_stack_guards_ok())
        helikon_panic(HELIKON_PANIC_STACK_GUARD, 0u, 0u);
    int paint_rc = paint_pass(ctx);
    if (paint_rc != HELIKON_OK)
        boot_fail(ctx, HELIKON_STAGE_PASS, paint_rc,
                  "The PASS frame did not reach the panel.");
    stage_pass(ctx, HELIKON_STAGE_PASS);
    helikon_clock_delay_us(&ctx->board.clock, 3000000u);
    boot_power_off(ctx);
}
