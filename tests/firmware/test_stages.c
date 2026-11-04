/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "error.h"
#include "stages.h"

#include "test_check.h"

static uint64_t fake_now;

static uint64_t now_us(void *context)
{
    (void)context;
    return fake_now;
}

int main(void)
{
    struct helikon_stages stages;
    helikon_stages_init(&stages, now_us, NULL);

    fake_now = 0;
    CHECK_EQ(HELIKON_OK, helikon_stages_begin(&stages, HELIKON_STAGE_BOOT));
    CHECK_EQ(HELIKON_ERR_BUSY,
             helikon_stages_begin(&stages, HELIKON_STAGE_PMU));
    CHECK_EQ(HELIKON_ERR_BUSY,
             helikon_stages_pass(&stages, HELIKON_STAGE_PMU));
    fake_now = 2000;
    CHECK_EQ(HELIKON_OK, helikon_stages_pass(&stages, HELIKON_STAGE_BOOT));
    CHECK_EQ(2, helikon_stages_elapsed_ms(&stages, HELIKON_STAGE_BOOT));

    fake_now = 2000;
    CHECK_EQ(HELIKON_OK, helikon_stages_begin(&stages, HELIKON_STAGE_PMU));
    fake_now = 5000;
    CHECK_EQ(HELIKON_OK, helikon_stages_pass(&stages, HELIKON_STAGE_PMU));
    CHECK_EQ(3, helikon_stages_elapsed_ms(&stages, HELIKON_STAGE_PMU));

    fake_now = 5000;
    CHECK_EQ(HELIKON_OK, helikon_stages_begin(&stages, HELIKON_STAGE_LCD));
    fake_now = 6000;
    CHECK_EQ(HELIKON_OK, helikon_stages_fail(&stages, HELIKON_STAGE_LCD,
                                             HELIKON_ERR_IO));
    const struct helikon_stage_record *record =
        helikon_stages_record(&stages, HELIKON_STAGE_LCD);
    CHECK(record != NULL);
    CHECK_EQ(HELIKON_STAGE_FAILED, record->result);
    CHECK_EQ(HELIKON_ERR_IO, record->error);

    /* Stages are sequential: LCD was consumed, so ATA is next. */
    CHECK_EQ(HELIKON_ERR_BUSY,
             helikon_stages_begin(&stages, HELIKON_STAGE_LCD));
    CHECK_EQ(HELIKON_OK, helikon_stages_begin(&stages, HELIKON_STAGE_ATA));
    CHECK(helikon_stages_current(&stages) == HELIKON_STAGE_ATA);
    fake_now += 2000000;
    CHECK(helikon_stages_expired(&stages, 1000));
    CHECK(!helikon_stages_expired(&stages, 5000));
    CHECK_EQ(HELIKON_OK, helikon_stages_pass(&stages, HELIKON_STAGE_ATA));

    CHECK(helikon_stage_code(HELIKON_STAGE_HFS) != NULL);
    CHECK(helikon_stages_record(&stages, HELIKON_STAGE_COUNT) == NULL);

    return helikon_test_report("stages");
}
