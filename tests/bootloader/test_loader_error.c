/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The loader's error strings, which nothing used to execute.
 *
 * `bootloader/loader_error.c` was the only file in the tree that no host test
 * ran, and it is what a failed boot prints on a 320x240 screen: if a code has no
 * string, or two codes share one, the report a user photographs says the wrong
 * thing. It also goes through `printf`-style formatting, so a stray `%` in a
 * message is a bug waiting for a format string.
 *
 * Checked here:
 *   - every enumerator returns a distinct, non-empty string;
 *   - none of them falls through to the unknown fallback;
 *   - the fallback is used for values outside the enum, including positives;
 *   - no message contains a `%` (they are printed as arguments);
 *   - the enum has no gaps: -1..-16 all answer with their own message.
 */
#include "loader_error.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test_check.h"

struct entry {
    int code;
    const char *expected;
};

/* The messages, spelled out on purpose: a test that reads them from the
 * implementation cannot notice a changed one. */
static const struct entry entries[] = {
    { HELIKON_LOADER_OK, "ok" },
    { HELIKON_LOADER_ERR_IO, "I/O error" },
    { HELIKON_LOADER_ERR_ARGUMENT, "invalid argument" },
    { HELIKON_LOADER_ERR_BUFFER, "buffer too small" },
    { HELIKON_LOADER_ERR_NOT_FOUND, "not found" },
    { HELIKON_LOADER_ERR_MAGIC, "bad manifest magic" },
    { HELIKON_LOADER_ERR_VERSION, "bad manifest version" },
    { HELIKON_LOADER_ERR_BOARD, "wrong board" },
    { HELIKON_LOADER_ERR_IMAGE_VERSION, "bad image version" },
    { HELIKON_LOADER_ERR_ADDRESS, "bad load address" },
    { HELIKON_LOADER_ERR_SIZE, "bad image size" },
    { HELIKON_LOADER_ERR_HASH, "image hash mismatch" },
    { HELIKON_LOADER_ERR_SIGNATURE, "signature rejected" },
    { HELIKON_LOADER_ERR_FLAGS, "unsupported manifest flags" },
    { HELIKON_LOADER_ERR_BOOTSTATE, "invalid boot state" },
    { HELIKON_LOADER_ERR_NO_IMAGE, "no bootable image" },
    { HELIKON_LOADER_ERR_UNSUPPORTED, "unsupported object" }
};

#define UNKNOWN "unknown loader error"
#define ENTRY_COUNT (sizeof(entries) / sizeof(entries[0]))

static int check_message(const char *message)
{
    CHECK(message != NULL);
    CHECK(message[0] != '\0');
    CHECK(strcmp(message, UNKNOWN) != 0);
    CHECK(strchr(message, '%') == NULL);
    return 0;
}

int main(void)
{
    /* Each code answers with its own message. */
    for (size_t i = 0; i < ENTRY_COUNT; ++i) {
        const char *message = helikon_loader_strerror(entries[i].code);
        CHECK(message != NULL);
        CHECK(strcmp(message, entries[i].expected) == 0);
        CHECK(check_message(message) == 0);
    }

    /* No two codes share a message: a report that says "bad image size" for a
     * bad signature is worse than no report. */
    for (size_t i = 0; i < ENTRY_COUNT; ++i) {
        for (size_t j = i + 1u; j < ENTRY_COUNT; ++j) {
            CHECK(strcmp(helikon_loader_strerror(entries[i].code),
                         helikon_loader_strerror(entries[j].code)) != 0);
        }
    }

    /* The enum has no holes between 0 and the last error. */
    for (int code = 0; code >= HELIKON_LOADER_ERR_UNSUPPORTED; --code) {
        int found = 0;
        for (size_t i = 0; i < ENTRY_COUNT; ++i) {
            if (entries[i].code == code)
                found = 1;
        }
        CHECK(found);
        CHECK(strcmp(helikon_loader_strerror(code), UNKNOWN) != 0);
    }

    /* Anything outside the enum gets the fallback, including a positive code
     * (a plain errno) and the far end of the range. */
    CHECK(strcmp(helikon_loader_strerror(1), UNKNOWN) == 0);
    CHECK(strcmp(helikon_loader_strerror(255), UNKNOWN) == 0);
    CHECK(strcmp(helikon_loader_strerror(HELIKON_LOADER_ERR_UNSUPPORTED - 1),
                 UNKNOWN) == 0);
    CHECK(strcmp(helikon_loader_strerror(INT32_MIN), UNKNOWN) == 0);
    CHECK(strcmp(helikon_loader_strerror(INT32_MAX), UNKNOWN) == 0);

    printf("%zu loader error messages, all distinct and format-safe\n",
           ENTRY_COUNT);
    return 0;
}
