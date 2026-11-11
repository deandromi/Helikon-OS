/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stddef.h>

#include "vic.h"
#include "error.h"

#include "test_check.h"

static int handler_calls;
static void *handler_context;

static void sample_handler(void *context)
{
    ++handler_calls;
    handler_context = context;
}

static void prepare(void)
{
    handler_calls = 0;
    handler_context = NULL;
    helikon_vic_test_reset();
    CHECK_EQ(HELIKON_OK, helikon_vic_init());
}

static void test_init_clears_both_banks(void)
{
    prepare();
    for (uint32_t bank = 0; bank < HELIKON_VIC_BANK_COUNT; ++bank) {
        CHECK_EQ(0u, helikon_vic_test_read(
                         bank, HELIKON_VIC_REG_INT_ENABLE));
        CHECK_EQ(0u, helikon_vic_test_read(bank, HELIKON_VIC_REG_SOFT_INT));
        CHECK_EQ(0u, helikon_vic_test_read(
                         bank, HELIKON_VIC_REG_INT_SELECT));
        CHECK_EQ(0u, helikon_vic_test_read(bank, HELIKON_VIC_REG_ADDRESS));
    }
    CHECK_EQ(0u, helikon_vic_spurious_count());
    CHECK_EQ(HELIKON_ERR_ARGUMENT, helikon_vic_pending(64u));
    CHECK_EQ(HELIKON_ERR_ARGUMENT, helikon_vic_enable(64u));
    CHECK_EQ(HELIKON_ERR_ARGUMENT, helikon_vic_disable(64u));
    CHECK_EQ(HELIKON_ERR_ARGUMENT, helikon_vic_software_trigger(64u));
    CHECK_EQ(HELIKON_ERR_ARGUMENT, helikon_vic_software_clear(64u));
}

static void test_register_and_dispatch(void)
{
    prepare();
    int marker = 7;
    CHECK_EQ(HELIKON_OK, helikon_vic_register(3u, sample_handler, &marker));
    CHECK_EQ(HELIKON_OK, helikon_vic_enable(3u));
    CHECK(helikon_vic_test_read(0u, HELIKON_VIC_REG_INT_ENABLE) ==
          (1u << 3));
    CHECK_EQ(0, helikon_vic_pending(3u));

    helikon_vic_test_set_pending(3u, 1);
    CHECK_EQ(1, helikon_vic_pending(3u));
    helikon_vic_dispatch();
    CHECK_EQ(1, handler_calls);
    CHECK(handler_context == &marker);
    CHECK_EQ(0, helikon_vic_pending(3u));
    CHECK_EQ(0u, helikon_vic_spurious_count());
    CHECK_EQ(0u, helikon_vic_test_read(0u, HELIKON_VIC_REG_ADDRESS));

    /* A second dispatch with no pending source must not call the handler. */
    helikon_vic_dispatch();
    CHECK_EQ(1, handler_calls);
    CHECK_EQ(0u, helikon_vic_spurious_count());
}

static void test_unregistered_is_spurious(void)
{
    prepare();
    CHECK_EQ(HELIKON_OK, helikon_vic_enable(5u));
    helikon_vic_test_set_pending(5u, 1);
    helikon_vic_dispatch();
    CHECK_EQ(0, handler_calls);
    CHECK_EQ(1u, helikon_vic_spurious_count());
    CHECK_EQ(0, helikon_vic_pending(5u));

    /* Bank 1 has no handler table; it is counted, not dispatched or panicked. */
    CHECK_EQ(HELIKON_OK, helikon_vic_enable(35u));
    helikon_vic_test_set_pending(35u, 1);
    helikon_vic_dispatch();
    CHECK_EQ(2u, helikon_vic_spurious_count());
    CHECK_EQ(0u, helikon_vic_test_read(1u, HELIKON_VIC_REG_ADDRESS));
}

static void test_unregister(void)
{
    prepare();
    CHECK_EQ(HELIKON_OK, helikon_vic_register(4u, sample_handler, NULL));
    CHECK_EQ(HELIKON_OK, helikon_vic_unregister(4u));
    CHECK_EQ(HELIKON_OK, helikon_vic_enable(4u));
    helikon_vic_test_set_pending(4u, 1);
    helikon_vic_dispatch();
    CHECK_EQ(0, handler_calls);
    CHECK_EQ(1u, helikon_vic_spurious_count());
}

static void test_software_trigger_and_clear(void)
{
    prepare();
    CHECK_EQ(HELIKON_OK, helikon_vic_software_trigger(7u));
    CHECK(helikon_vic_test_read(0u, HELIKON_VIC_REG_SOFT_INT) ==
          (1u << 7));
    CHECK_EQ(HELIKON_OK, helikon_vic_software_clear(7u));
    CHECK_EQ(0u,
             helikon_vic_test_read(0u, HELIKON_VIC_REG_SOFT_INT));
}

static void test_invalid_register(void)
{
    prepare();
    CHECK_EQ(HELIKON_ERR_ARGUMENT,
             helikon_vic_register(HELIKON_VIC_HANDLER_COUNT,
                                  sample_handler, NULL));
    CHECK_EQ(HELIKON_ERR_ARGUMENT, helikon_vic_register(2u, NULL, NULL));
    CHECK_EQ(HELIKON_ERR_ARGUMENT,
             helikon_vic_unregister(HELIKON_VIC_HANDLER_COUNT));
}

int main(void)
{
    test_init_clears_both_banks();
    test_register_and_dispatch();
    test_unregistered_is_spurious();
    test_unregister();
    test_software_trigger_and_clear();
    test_invalid_register();
    return helikon_test_report("hal/vic");
}
