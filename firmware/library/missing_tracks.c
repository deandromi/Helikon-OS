/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * "3 of 12 tracks", and which ones are missing. See `missing_tracks.h` for the
 * three refusals and why matching is by number.
 *
 * Two passes and one 128-bit... no: one 128-byte bitmap's worth of work in a
 * fixed `present` array. The numbers are 1-based and bounded by
 * `HELIKON_MISSING_MAX`, so presence is a bit test, not a search.
 */
#include "missing_tracks.h"

#include <stddef.h>
#include <stdint.h>

#include "format.h"
#include "mem.h"
#include "string.h"

/* Present-if-set, index = track number. Static so nothing is allocated, and
 * cleared per call so a report cannot inherit the previous album's numbers. */
static uint8_t present[(HELIKON_MISSING_MAX + 8u) / 8u];

void helikon_missing_report(const uint16_t *numbers, size_t count,
                            uint32_t stated_total, uint32_t disc_count,
                            struct helikon_missing_report *report)
{
    if (report == NULL)
        return;

    helikon_memset(report, 0, sizeof(*report));
    helikon_memset(present, 0, sizeof(present));

    report->stated_total = stated_total;
    report->reason = HELIKON_MISSING_KNOWN;

    if (stated_total == 0u)
        report->reason = HELIKON_MISSING_NO_TOTAL;
    else if (disc_count > 1u)
        report->reason = HELIKON_MISSING_MULTI_DISC;
    else if (stated_total > HELIKON_MISSING_MAX)
        report->reason = HELIKON_MISSING_TOTAL_TOO_LARGE;

    report->known = (uint8_t)(report->reason == HELIKON_MISSING_KNOWN ? 1u : 0u);

    size_t index = 0u;
    while (index < count && numbers != NULL) {
        uint16_t number = numbers[index];
        ++index;
        if (number == 0u) {
            ++report->unnumbered;
            continue;
        }
        ++report->numbered;
        /* A number above the stated total is inconsistent tags: count it, do
         * not give it a slot, and do not invent a thirteenth track. */
        if (number > stated_total || stated_total == 0u)
            continue;
        if (report->known == 0u)
            continue;
        present[number >> 3] = (uint8_t)(present[number >> 3] | (1u << (number & 7u)));
    }

    if (report->known == 0u)
        return;

    uint32_t slot = 1u;
    while (slot <= stated_total) {
        if ((present[slot >> 3] & (1u << (slot & 7u))) == 0u &&
            report->missing_count < HELIKON_MISSING_MAX) {
            report->missing[report->missing_count] = (uint16_t)slot;
            ++report->missing_count;
        }
        ++slot;
    }
}

/* Appends `text` to `buffer` at `*used`, never past `capacity`, and returns the
 * new length. Truncation is silent here because the caller checks the result. */
static size_t append(char *buffer, size_t capacity, size_t used,
                     const char *text)
{
    if (used >= capacity)
        return used;
    size_t room = capacity - used;
    size_t length = helikon_strlen(text, capacity);
    if (length >= room) {
        helikon_memcpy(buffer + used, text, room - 1u);
        buffer[capacity - 1u] = '\0';
        return capacity - 1u;
    }
    helikon_memcpy(buffer + used, text, length);
    buffer[used + length] = '\0';
    return used + length;
}

static size_t append_u32(char *buffer, size_t capacity, size_t used,
                         uint32_t value)
{
    if (used >= capacity)
        return used;
    char number[11];
    (void)helikon_format_u32(number, sizeof(number), value);
    return append(buffer, capacity, used, number[0] != '\0' ? number : "0");
}

/* The album name is a heading, not a paragraph: past 28 bytes it is cut with
 * `...` so the line stays one line on a 320-pixel panel. */
static size_t append_name(char *buffer, size_t capacity, size_t used,
                          const char *name)
{
    char trimmed[32];
    size_t index = 0u;
    while (index + 1u < 29u && name[index] != '\0') {
        trimmed[index] = name[index];
        ++index;
    }
    if (name[index] != '\0' && index >= 2u) {
        trimmed[index - 2u] = '.';
        trimmed[index - 1u] = '.';
        trimmed[index] = '.';
        ++index;
    }
    trimmed[index] = '\0';
    return append(buffer, capacity, used, trimmed);
}

size_t helikon_missing_text(const struct helikon_missing_report *report,
                            const char *album_name, char *buffer,
                            size_t capacity)
{
    if (buffer == NULL || capacity == 0u)
        return 0u;
    buffer[0] = '\0';
    if (report == NULL)
        return 0u;

    size_t used = 0u;
    used = append(buffer, capacity, used, "Album: ");
    used = append_name(buffer, capacity, used,
                       album_name != NULL ? album_name : "");

    if (report->known == 0u) {
        used = append(buffer, capacity, used, "\nTracks: ");
        used = append_u32(buffer, capacity, used, report->numbered);
        used = append(buffer, capacity, used, " present, ");
        if (report->reason == HELIKON_MISSING_NO_TOTAL)
            used = append(buffer, capacity, used, "total not stated\n");
        else if (report->reason == HELIKON_MISSING_MULTI_DISC)
            used = append(buffer, capacity, used, "multi-disc\n");
        else
            used = append(buffer, capacity, used, "total too large\n");
        if (report->unnumbered != 0u) {
            used = append(buffer, capacity, used, "Unnumbered: ");
            used = append_u32(buffer, capacity, used, report->unnumbered);
            used = append(buffer, capacity, used, "\n");
        }
        return used;
    }

    uint32_t found = report->stated_total - report->missing_count;
    used = append(buffer, capacity, used, "\nTracks: ");
    used = append_u32(buffer, capacity, used, found);
    used = append(buffer, capacity, used, " of ");
    used = append_u32(buffer, capacity, used, report->stated_total);
    used = append(buffer, capacity, used, " stated\n");

    if (report->unnumbered != 0u) {
        used = append(buffer, capacity, used, "Unnumbered: ");
        used = append_u32(buffer, capacity, used, report->unnumbered);
        used = append(buffer, capacity, used, "\n");
    }
    if (report->missing_count == 0u) {
        used = append(buffer, capacity, used, "Missing: none\n");
        return used;
    }

    used = append(buffer, capacity, used, "Missing:");
    uint32_t index = 0u;
    while (index < report->missing_count) {
        used = append(buffer, capacity, used, index == 0u ? " " : ", ");
        used = append_u32(buffer, capacity, used, report->missing[index]);
        ++index;
    }
    used = append(buffer, capacity, used, "\n");
    return used;
}
