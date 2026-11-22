/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Host tests for playlist import (T-093, the import half).
 *
 * The parser is the part of a playlist feature that has to be paranoid: the file
 * comes from a laptop, a download or a hand edit, and everything about it is a
 * claim. Each test below is one of the rules stated in `playlist.h`, because a
 * rule that is only in a comment is not a rule.
 */
#include "playlist.h"

#include <stdio.h>
#include <string.h>

#include "helikon_error.h"

static unsigned int failures;

static int expect(int condition, const char *message)
{
    if (!condition) {
        printf("FAIL %s\n", message);
        failures += 1u;
        return 1;
    }
    return 0;
}

static int open_text(struct helikon_playlist_parser *parser, const char *text,
                     const char *base, uint8_t format)
{
    return helikon_playlist_open(parser, text, strlen(text), base, format);
}

/* Collect the entries into a readable shape, then assert on that. */
static int collect(struct helikon_playlist_parser *parser, char paths[][128],
                   size_t capacity, size_t *count)
{
    struct helikon_playlist_entry entry;
    *count = 0u;
    for (;;) {
        int rc = helikon_playlist_next(parser, &entry);
        if (rc < 0)
            return rc;
        if (rc == 0)
            return 0;
        if (*count < capacity)
            snprintf(paths[*count], 128, "%s", entry.path);
        ++*count;
    }
}

static int check_basic_m3u(void)
{
    struct helikon_playlist_parser parser;
    char paths[8][128];
    size_t count = 0u;

    if (open_text(&parser, "one.mp3\ntwo.flac\n", "/Music",
                  HELIKON_PLAYLIST_AUTO) != HELIKON_OK)
        return expect(0, "an m3u opens");
    if (collect(&parser, paths, 8u, &count) != 0)
        return expect(0, "an m3u parses");
    if (expect(count == 2u, "two entries"))
        return 1;
    if (expect(strcmp(paths[0], "/Music/one.mp3") == 0, "relative entry resolves"))
        return 1;
    if (expect(strcmp(paths[1], "/Music/two.flac") == 0, "second entry resolves"))
        return 1;
    if (expect(parser.skipped == 0u && parser.emitted == 2u,
               "nothing was skipped"))
        return 1;
    return 0;
}

static int check_comments_bom_crlf(void)
{
    struct helikon_playlist_parser parser;
    char paths[8][128];
    size_t count = 0u;

    const char text[] = "\xef\xbb\xbf#EXTM3U\r\n# a comment\r\n\r\n"
                        "  spaced name.mp3  \r\n#EXTM3U\r\nlast.mp3\r\n";
    if (open_text(&parser, text, "/", HELIKON_PLAYLIST_AUTO) != HELIKON_OK)
        return expect(0, "a commented list opens");
    if (collect(&parser, paths, 8u, &count) != 0)
        return expect(0, "a commented list parses");
    if (expect(count == 2u, "comments and blank lines are not entries"))
        return 1;
    if (expect(strcmp(paths[0], "/spaced name.mp3") == 0,
               "spaces are trimmed inside a path"))
        return 1;
    if (expect(strcmp(paths[1], "/last.mp3") == 0,
               "the last line parses without a newline"))
        return 1;
    return 0;
}

static int check_extinf(void)
{
    struct helikon_playlist_parser parser;
    struct helikon_playlist_entry entry;

    const char text[] = "#EXTINF:215.42,Artist - Title, with comma\n"
                        "song.mp3\n"
                        "#EXTINF:-1,No Duration\n"
                        "other.mp3\n";
    if (open_text(&parser, text, "/Music", HELIKON_PLAYLIST_M3U) != HELIKON_OK)
        return expect(0, "extinf list opens");
    if (expect(helikon_playlist_next(&parser, &entry) == 1, "first entry"))
        return 1;
    if (expect(entry.has_duration && entry.duration_s == 215u,
               "fractional duration is truncated to seconds"))
        return 1;
    if (expect(entry.has_title &&
                   strcmp(entry.title, "Artist - Title, with comma") == 0,
               "the title keeps its commas"))
        return 1;
    if (expect(helikon_playlist_next(&parser, &entry) == 1, "second entry"))
        return 1;
    if (expect(!entry.has_duration, "a negative duration is unknown, not zero"))
        return 1;
    if (expect(entry.has_title && strcmp(entry.title, "No Duration") == 0,
               "title without a duration"))
        return 1;
    if (expect(helikon_playlist_next(&parser, &entry) == 0, "the list ends"))
        return 1;
    return 0;
}

static int check_title_survives_blank_line(void)
{
    struct helikon_playlist_parser parser;
    struct helikon_playlist_entry entry;

    /* An EXTINF with no duration field, and a blank line before the path: the
     * title still belongs to the next entry, which is what players do. */
    const char text[] = "#EXTINF:Only A Title,There\n\nsong.mp3\n";
    if (open_text(&parser, text, "/", HELIKON_PLAYLIST_M3U) != HELIKON_OK)
        return expect(0, "opens");
    if (expect(helikon_playlist_next(&parser, &entry) == 1, "one entry"))
        return 1;
    if (expect(entry.has_title && strcmp(entry.title, "There") == 0,
               "title survives a blank line"))
        return 1;
    return 0;
}

static int check_path_resolution(void)
{
    struct helikon_playlist_parser parser;
    char paths[8][128];
    size_t count = 0u;

    const char text[] = "a/./b.mp3\n../up.mp3\n/absolute.mp3\n"
                        "sub/../x.mp3\n";
    if (open_text(&parser, text, "/Music/Owner", HELIKON_PLAYLIST_M3U) !=
        HELIKON_OK)
        return expect(0, "opens");
    if (collect(&parser, paths, 8u, &count) != 0)
        return expect(0, "parses");
    if (expect(count == 4u, "four entries"))
        return 1;
    if (expect(strcmp(paths[0], "/Music/Owner/a/b.mp3") == 0, "'.' is dropped"))
        return 1;
    if (expect(strcmp(paths[1], "/Music/up.mp3") == 0, "'..' pops a component"))
        return 1;
    if (expect(strcmp(paths[2], "/absolute.mp3") == 0,
               "an absolute entry stays absolute"))
        return 1;
    if (expect(strcmp(paths[3], "/Music/Owner/x.mp3") == 0,
               "a component can be popped and replaced"))
        return 1;
    return 0;
}

static int check_escape_and_bad_entries(void)
{
    struct helikon_playlist_parser parser;
    char paths[8][128];
    size_t count = 0u;

    /* An entry that escapes the root, one with a control character, one with an
     * invalid UTF-8 sequence, and a path far past the entry limit. Each is
     * refused and counted; the good entry after them still arrives. */
    char text[1200];
    size_t used = (size_t)snprintf(text, sizeof(text), "../outside.mp3\n");
    const char control[] = { (char)0x01, 'b', 'a', 'd', '.', 'm', 'p', '3', '\n' };
    memcpy(text + used, control, sizeof(control));
    used += sizeof(control);
    const char invalid[] = { (char)0xc3, (char)0x28, '.', 'm', 'p', '3', '\n' };
    memcpy(text + used, invalid, sizeof(invalid));
    used += sizeof(invalid);
    memset(text + used, 'x', HELIKON_PLAYLIST_PATH_MAX + 32u);
    used += HELIKON_PLAYLIST_PATH_MAX + 32u;
    text[used++] = '\n';
    const char good[] = "good.mp3\n";
    memcpy(text + used, good, sizeof(good) - 1u);
    used += sizeof(good) - 1u;
    text[used] = '\0';

    if (open_text(&parser, text, "/", HELIKON_PLAYLIST_M3U) != HELIKON_OK)
        return expect(0, "opens");
    if (collect(&parser, paths, 8u, &count) != 0)
        return expect(0, "parses");
    if (expect(count == 1u, "only the good entry is produced"))
        return 1;
    if (expect(strcmp(paths[0], "/good.mp3") == 0, "the good entry is last"))
        return 1;
    if (expect(parser.skipped == 4u, "all four bad entries are counted"))
        return 1;
    return 0;
}

static int check_nul_and_arguments(void)
{
    struct helikon_playlist_parser parser;
    struct helikon_playlist_entry entry;

    const char nul_text[] = { 'a', '.', 'm', 'p', '3', '\0', 'b', '.',
                              'm', 'p', '3' };
    if (expect(helikon_playlist_open(&parser, nul_text, sizeof(nul_text), "/",
                                     HELIKON_PLAYLIST_M3U) == HELIKON_ERR_FORMAT,
               "a NUL byte is refused, not treated as the end"))
        return 1;
    if (expect(helikon_playlist_open(NULL, "a", 1, "/", HELIKON_PLAYLIST_M3U) ==
                   HELIKON_ERR_ARGUMENT,
               "a NULL parser is refused"))
        return 1;
    if (expect(helikon_playlist_open(&parser, "a", 1, "relative",
                                     HELIKON_PLAYLIST_M3U) == HELIKON_ERR_ARGUMENT,
               "a relative base is refused"))
        return 1;
    if (expect(open_text(&parser, "", "/", HELIKON_PLAYLIST_AUTO) == HELIKON_OK,
               "an empty file is an empty playlist"))
        return 1;
    if (expect(helikon_playlist_next(&parser, &entry) == 0, "and yields nothing"))
        return 1;
    if (expect(helikon_playlist_next(&parser, NULL) == HELIKON_ERR_ARGUMENT,
               "a NULL entry is refused"))
        return 1;
    return 0;
}

static int check_pls(void)
{
    struct helikon_playlist_parser parser;
    struct helikon_playlist_entry entry;

    /* Title before its File line, a gap at index 3 and Length=-1: all three are
     * things real .pls files contain. */
    const char text[] =
        "[playlist]\n"
        "Title2=Second\n"
        "File2=/Music/second.mp3\n"
        "File1=/Music/first.mp3\n"
        "Title1=First\n"
        "Length1=180\n"
        "Length2=-1\n"
        "File4=/Music/fourth.mp3\n"
        "NumberOfEntries=3\n"
        "Version=2\n";

    if (open_text(&parser, text, "/", HELIKON_PLAYLIST_AUTO) != HELIKON_OK)
        return expect(0, "a PLS opens");
    if (expect(parser.format == HELIKON_PLAYLIST_PLS, "PLS is detected"))
        return 1;

    if (expect(helikon_playlist_next(&parser, &entry) == 1, "first PLS entry"))
        return 1;
    if (expect(strcmp(entry.path, "/Music/first.mp3") == 0, "index 1 comes first"))
        return 1;
    if (expect(entry.has_title && strcmp(entry.title, "First") == 0,
               "title after its file line"))
        return 1;
    if (expect(entry.has_duration && entry.duration_s == 180u,
               "length in seconds"))
        return 1;

    if (expect(helikon_playlist_next(&parser, &entry) == 1, "second PLS entry"))
        return 1;
    if (expect(strcmp(entry.path, "/Music/second.mp3") == 0, "index 2"))
        return 1;
    if (expect(entry.has_title && strcmp(entry.title, "Second") == 0,
               "title before its file line"))
        return 1;
    if (expect(!entry.has_duration, "Length=-1 is unknown"))
        return 1;

    if (expect(helikon_playlist_next(&parser, &entry) == 1, "third PLS entry"))
        return 1;
    if (expect(strcmp(entry.path, "/Music/fourth.mp3") == 0,
               "a missing index is skipped, not fatal"))
        return 1;

    if (expect(helikon_playlist_next(&parser, &entry) == 0, "the list ends"))
        return 1;
    return 0;
}

static int check_capacity(void)
{
    struct helikon_playlist_parser parser;
    struct helikon_playlist_entry entry;

    static char text[(HELIKON_PLAYLIST_MAX_ENTRIES + 8u) * 16u];
    size_t used = 0u;
    for (uint32_t index = 0u; index < HELIKON_PLAYLIST_MAX_ENTRIES + 8u;
         ++index) {
        int written = snprintf(text + used, sizeof(text) - used, "t%u.mp3\n",
                               (unsigned)index);
        if (written <= 0)
            return expect(0, "the fixture fits");
        used += (size_t)written;
    }

    if (open_text(&parser, text, "/", HELIKON_PLAYLIST_M3U) != HELIKON_OK)
        return expect(0, "a long list opens");
    uint32_t count = 0u;
    for (;;) {
        int rc = helikon_playlist_next(&parser, &entry);
        if (rc < 0)
            return expect(0, "a long list parses");
        if (rc == 0)
            break;
        ++count;
    }
    if (expect(count == HELIKON_PLAYLIST_MAX_ENTRIES,
               "the list stops at the entry limit instead of overflowing"))
        return 1;
    return 0;
}

int main(void)
{
    if (check_basic_m3u() || check_comments_bom_crlf() || check_extinf() ||
        check_title_survives_blank_line() || check_path_resolution() ||
        check_escape_and_bad_entries() || check_nul_and_arguments() ||
        check_pls() || check_capacity())
        return 1;
    if (failures != 0u) {
        printf("FAIL playlist import\n");
        return 1;
    }
    printf("PASS playlist import\n");
    return 0;
}
