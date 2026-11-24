/*
 * "3 of 12 tracks" -- the numbering core, `firmware/library/missing_tracks.c`.
 *
 * The feature exists for libraries whose tags disagree with their files, so the
 * cases that matter are the awkward ones: no stated total, a multi-disc album,
 * a duplicate number, a number above the total, a track with no number at all,
 * and a name too long for the panel. Every one of them must produce either a
 * defensible claim or no claim -- never a plausible guess.
 */
#include "missing_tracks.h"

#include <stdio.h>
#include <string.h>

#include "test_check.h"

static size_t length_of(const char *text)
{
    size_t length = 0u;
    while (text[length] != '\0')
        ++length;
    return length;
}

/* `firmware/lib/string.h` shadows the system header on this include path, so
 * the test carries the two helpers it needs instead of pulling in libc. */
static int contains(const char *haystack, const char *needle)
{
    size_t needle_length = length_of(needle);
    if (needle_length == 0u)
        return 1;
    while (*haystack != '\0') {
        size_t index = 0u;
        while (index < needle_length && haystack[index] == needle[index])
            ++index;
        if (index == needle_length)
            return 1;
        ++haystack;
    }
    return 0;
}

static struct helikon_missing_report report;
static char text[HELIKON_MISSING_TEXT_MAX];

static int shows(const char *want)
{
    size_t used = helikon_missing_text(&report, "Kind of Blue", text,
                                       sizeof(text));
    if (used != length_of(text)) {
        fprintf(stderr, "  FAIL text length %zu, strlen %zu\n", used,
                length_of(text));
        return 1;
    }
    if (!contains(text, want)) {
        fprintf(stderr, "  FAIL text lacks \"%s\":\n%s\n", want, text);
        return 1;
    }
    return 0;
}

/* Every number present: nothing is missing and the claim is made. */
static int check_complete(void)
{
    static const uint16_t numbers[12] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    helikon_missing_report(numbers, 12u, 12u, 1u, &report);
    CHECK(report.known == 1u);
    CHECK(report.reason == HELIKON_MISSING_KNOWN);
    CHECK(report.missing_count == 0u);
    CHECK(report.numbered == 12u);
    CHECK(report.unnumbered == 0u);
    return shows("Tracks: 12 of 12 stated") || shows("Missing: none");
}

/* Gaps in the middle and at both ends. */
static int check_partial(void)
{
    static const uint16_t numbers[3] = {1, 2, 5};
    helikon_missing_report(numbers, 3u, 12u, 1u, &report);
    CHECK(report.known == 1u);
    CHECK(report.missing_count == 9u);
    CHECK(report.missing[0] == 3u);
    CHECK(report.missing[1] == 4u);
    CHECK(report.missing[2] == 6u);
    CHECK(report.missing[8] == 12u);
    if (shows("Tracks: 3 of 12 stated"))
        return 1;
    return shows("Missing: 3, 4, 6, 7, 8, 9, 10, 11, 12");
}

/* A total nobody stated is not a gap list. */
static int check_no_total(void)
{
    static const uint16_t numbers[3] = {1, 2, 3};
    helikon_missing_report(numbers, 3u, 0u, 1u, &report);
    CHECK(report.known == 0u);
    CHECK(report.reason == HELIKON_MISSING_NO_TOTAL);
    CHECK(report.missing_count == 0u);
    CHECK(report.numbered == 3u);
    return shows("total not stated");
}

/* A multi-disc album's total counts discs; the per-disc total is not in the
 * database, so "9 of 12" would be a number nobody wrote down. */
static int check_multi_disc(void)
{
    static const uint16_t numbers[2] = {1, 2};
    helikon_missing_report(numbers, 2u, 24u, 2u, &report);
    CHECK(report.known == 0u);
    CHECK(report.reason == HELIKON_MISSING_MULTI_DISC);
    CHECK(report.missing_count == 0u);
    return shows("multi-disc");
}

/* A list longer than the report can hold is a half list, and a half list lies. */
static int check_total_too_large(void)
{
    static const uint16_t numbers[1] = {1};
    helikon_missing_report(numbers, 1u, HELIKON_MISSING_MAX + 1u, 1u, &report);
    CHECK(report.known == 0u);
    CHECK(report.reason == HELIKON_MISSING_TOTAL_TOO_LARGE);
    CHECK(report.missing_count == 0u);
    return shows("total too large");
}

/* A track with no number never fills a gap: nothing says which gap. */
static int check_unnumbered(void)
{
    static const uint16_t numbers[3] = {0, 3, 0};
    helikon_missing_report(numbers, 3u, 5u, 1u, &report);
    CHECK(report.known == 1u);
    CHECK(report.numbered == 1u);
    CHECK(report.unnumbered == 2u);
    CHECK(report.missing_count == 4u);
    CHECK(report.missing[0] == 1u);
    CHECK(report.missing[1] == 2u);
    CHECK(report.missing[2] == 4u);
    CHECK(report.missing[3] == 5u);
    if (shows("Unnumbered: 2"))
        return 1;
    return shows("Missing: 1, 2, 4, 5");
}

/* Two files claiming track 3: one slot filled, two tracks counted. */
static int check_duplicate(void)
{
    static const uint16_t numbers[2] = {3, 3};
    helikon_missing_report(numbers, 2u, 3u, 1u, &report);
    CHECK(report.numbered == 2u);
    CHECK(report.missing_count == 2u);
    CHECK(report.missing[0] == 1u);
    CHECK(report.missing[1] == 2u);
    return 0;
}

/* Thirteen of twelve is inconsistent tags: counted, not given a slot. */
static int check_above_total(void)
{
    static const uint16_t numbers[1] = {13};
    helikon_missing_report(numbers, 1u, 12u, 1u, &report);
    CHECK(report.numbered == 1u);
    CHECK(report.missing_count == 12u);
    CHECK(report.known == 1u);
    return 0;
}

/* No tracks at all, with a stated total: every number is missing. */
static int check_empty(void)
{
    helikon_missing_report(NULL, 0u, 4u, 1u, &report);
    CHECK(report.known == 1u);
    CHECK(report.numbered == 0u);
    CHECK(report.missing_count == 4u);

    helikon_missing_report(NULL, 0u, 0u, 1u, &report);
    CHECK(report.known == 0u);
    CHECK(report.reason == HELIKON_MISSING_NO_TOTAL);
    return 0;
}

/* A truncated line still ends in a NUL and never writes past the buffer. */
static int check_truncation(void)
{
    static const uint16_t numbers[1] = {1};
    char small[24];
    memset(small, 'x', sizeof(small));
    helikon_missing_report(numbers, 1u, 128u, 1u, &report);
    size_t used = helikon_missing_text(&report, "A very long album name here",
                                       small, sizeof(small));
    CHECK(used < sizeof(small));
    CHECK(small[sizeof(small) - 1u] == '\0');
    CHECK(length_of(small) == used);

    /* Zero capacity is not a crash. */
    CHECK(helikon_missing_text(&report, "x", small, 0u) == 0u);
    CHECK(helikon_missing_text(NULL, "x", small, sizeof(small)) == 0u);
    return 0;
}

/* The heading is cut with `...` instead of splitting the line. */
static int check_long_name(void)
{
    static const uint16_t numbers[1] = {1};
    helikon_missing_report(numbers, 1u, 2u, 1u, &report);
    (void)helikon_missing_text(&report, "An Extremely Long Album Title Indeed",
                               text, sizeof(text));
    CHECK(contains(text, "Album: An Extremely Long Album"));
    return 0;
}

int main(void)
{
    if (check_complete())
        return 1;
    if (check_partial())
        return 1;
    if (check_no_total())
        return 1;
    if (check_multi_disc())
        return 1;
    if (check_total_too_large())
        return 1;
    if (check_unnumbered())
        return 1;
    if (check_duplicate())
        return 1;
    if (check_above_total())
        return 1;
    if (check_empty())
        return 1;
    if (check_truncation())
        return 1;
    if (check_long_name())
        return 1;
    printf("PASS missing tracks: a stated total is claimed, a missing one is "
           "not, and no gap is invented from a track with no number\n");
    return 0;
}
