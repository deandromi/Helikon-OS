/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Boot-stage state machine and the stable F1 stage vocabulary.
 *
 * The stage codes are part of the bring-up contract: a photograph of the
 * screen must identify where a boot stopped. The state machine rejects a stage
 * that is started twice, out of order, or completed before it started. It has
 * no hardware dependency; a caller-supplied monotonic clock supplies timing.
 */
#ifndef HELIKON_DIAG_STAGES_H
#define HELIKON_DIAG_STAGES_H

#include <stdint.h>

enum helikon_stage {
    HELIKON_STAGE_BOOT = 0,
    HELIKON_STAGE_PMU,
    HELIKON_STAGE_LCD,
    HELIKON_STAGE_ATA,
    HELIKON_STAGE_PART,
    HELIKON_STAGE_HFS,
    HELIKON_STAGE_READ,
    HELIKON_STAGE_INPUT,
    HELIKON_STAGE_WRITE,
    HELIKON_STAGE_PASS,
    HELIKON_STAGE_FAIL,
    HELIKON_STAGE_COUNT
};

enum helikon_stage_result {
    HELIKON_STAGE_PENDING = 0,
    HELIKON_STAGE_PASSED = 1,
    HELIKON_STAGE_FAILED = -1
};

struct helikon_stage_record {
    uint64_t started_us;
    uint64_t ended_us;
    int result;
    int error;
};

struct helikon_stages {
    uint64_t (*now_us)(void *context);
    void *context;
    int next;
    int current;
    struct helikon_stage_record records[HELIKON_STAGE_COUNT];
};

void helikon_stages_init(struct helikon_stages *stages,
                         uint64_t (*now_us)(void *context), void *context);

/* Returns HELIKON_OK or HELIKON_ERR_BUSY when the order is wrong. */
int helikon_stages_begin(struct helikon_stages *stages, int stage);
int helikon_stages_pass(struct helikon_stages *stages, int stage);
int helikon_stages_fail(struct helikon_stages *stages, int stage, int error);

/* 1 when the current stage has exceeded `timeout_ms`, else 0. */
int helikon_stages_expired(const struct helikon_stages *stages,
                           uint32_t timeout_ms);

const struct helikon_stage_record *helikon_stages_record(
    const struct helikon_stages *stages, int stage);
uint64_t helikon_stages_elapsed_ms(const struct helikon_stages *stages,
                                   int stage);
int helikon_stages_current(const struct helikon_stages *stages);

const char *helikon_stage_code(int stage);
const char *helikon_stage_text(int stage);

#endif
