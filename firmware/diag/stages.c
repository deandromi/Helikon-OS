/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "stages.h"

#include <stddef.h>

#include "error.h"

void helikon_stages_init(struct helikon_stages *stages,
                         uint64_t (*now_us)(void *context), void *context)
{
    if (stages == NULL)
        return;
    stages->now_us = now_us;
    stages->context = context;
    stages->next = HELIKON_STAGE_BOOT;
    stages->current = -1;
    for (int i = 0; i < HELIKON_STAGE_COUNT; ++i) {
        stages->records[i].started_us = 0;
        stages->records[i].ended_us = 0;
        stages->records[i].result = HELIKON_STAGE_PENDING;
        stages->records[i].error = HELIKON_OK;
    }
}

int helikon_stages_begin(struct helikon_stages *stages, int stage)
{
    if (stages == NULL || stage < 0 || stage >= HELIKON_STAGE_COUNT)
        return HELIKON_ERR_ARGUMENT;
    if (stage != stages->next || stages->current >= 0)
        return HELIKON_ERR_BUSY;

    stages->records[stage].started_us =
        stages->now_us != NULL ? stages->now_us(stages->context) : 0u;
    stages->records[stage].ended_us = 0;
    stages->records[stage].result = HELIKON_STAGE_PENDING;
    stages->records[stage].error = HELIKON_OK;
    stages->current = stage;
    ++stages->next;
    return HELIKON_OK;
}

int helikon_stages_pass(struct helikon_stages *stages, int stage)
{
    if (stages == NULL || stage < 0 || stage >= HELIKON_STAGE_COUNT)
        return HELIKON_ERR_ARGUMENT;
    if (stage != stages->current || stages->records[stage].result !=
                                        HELIKON_STAGE_PENDING)
        return HELIKON_ERR_BUSY;

    stages->records[stage].ended_us =
        stages->now_us != NULL ? stages->now_us(stages->context) : 0u;
    stages->records[stage].result = HELIKON_STAGE_PASSED;
    stages->current = -1;
    return HELIKON_OK;
}

int helikon_stages_fail(struct helikon_stages *stages, int stage, int error)
{
    if (stages == NULL || stage < 0 || stage >= HELIKON_STAGE_COUNT)
        return HELIKON_ERR_ARGUMENT;
    if (stage != stages->current || stages->records[stage].result !=
                                        HELIKON_STAGE_PENDING)
        return HELIKON_ERR_BUSY;

    stages->records[stage].ended_us =
        stages->now_us != NULL ? stages->now_us(stages->context) : 0u;
    stages->records[stage].result = HELIKON_STAGE_FAILED;
    stages->records[stage].error = error;
    stages->current = -1;
    return HELIKON_OK;
}

int helikon_stages_expired(const struct helikon_stages *stages,
                           uint32_t timeout_ms)
{
    if (stages == NULL || stages->current < 0 || stages->now_us == NULL)
        return 0;

    const struct helikon_stage_record *record =
        &stages->records[stages->current];
    uint64_t now = stages->now_us(stages->context);
    if (now < record->started_us)
        return 0;
    return now - record->started_us >
           (uint64_t)timeout_ms * 1000u;
}

const struct helikon_stage_record *helikon_stages_record(
    const struct helikon_stages *stages, int stage)
{
    if (stages == NULL || stage < 0 || stage >= HELIKON_STAGE_COUNT)
        return NULL;
    return &stages->records[stage];
}

uint64_t helikon_stages_elapsed_ms(const struct helikon_stages *stages,
                                   int stage)
{
    const struct helikon_stage_record *record =
        helikon_stages_record(stages, stage);
    if (record == NULL || record->result == HELIKON_STAGE_PENDING)
        return 0u;
    if (record->ended_us < record->started_us)
        return 0u;
    return (record->ended_us - record->started_us) / 1000u;
}

int helikon_stages_current(const struct helikon_stages *stages)
{
    return stages == NULL ? -1 : stages->current;
}

const char *helikon_stage_code(int stage)
{
    switch (stage) {
    case HELIKON_STAGE_BOOT: return "BOOT";
    case HELIKON_STAGE_PMU: return "PMU";
    case HELIKON_STAGE_LCD: return "LCD";
    case HELIKON_STAGE_ATA: return "ATA";
    case HELIKON_STAGE_PART: return "PART";
    case HELIKON_STAGE_HFS: return "HFS";
    case HELIKON_STAGE_READ: return "READ";
    case HELIKON_STAGE_INPUT: return "INPUT";
    case HELIKON_STAGE_PASS: return "PASS";
    case HELIKON_STAGE_FAIL: return "FAIL";
    default: return "?";
    }
}

const char *helikon_stage_text(int stage)
{
    switch (stage) {
    case HELIKON_STAGE_BOOT: return "HELIKON-OS";
    case HELIKON_STAGE_PMU: return "Power OK";
    case HELIKON_STAGE_LCD: return "Display OK";
    case HELIKON_STAGE_ATA: return "Storage OK";
    case HELIKON_STAGE_PART: return "Volume found";
    case HELIKON_STAGE_HFS: return "HFS+ OK";
    case HELIKON_STAGE_READ: return "Read OK";
    case HELIKON_STAGE_INPUT: return "Input OK";
    case HELIKON_STAGE_PASS: return "PASS";
    case HELIKON_STAGE_FAIL: return "FAIL";
    default: return "?";
    }
}
