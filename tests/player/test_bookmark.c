/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Resume bookmark text (T-093): the exact bytes, the round trip, and every
 * refusal. The file half lives in tests/vfs/test_vfs_bookmark.c; this suite
 * needs no filesystem, so it can be unforgiving about the format.
 */
#include <stdio.h>
#include <string.h>

#include "bookmark.h"
#include "helikon_error.h"

static int failures;

static int expect(int condition, const char *what)
{
    if (!condition) {
        printf("FAIL %s\n", what);
        failures += 1;
        return 1;
    }
    return 0;
}

static void fill(struct helikon_bookmark *bookmark)
{
    memset(bookmark, 0, sizeof(*bookmark));
    strcpy(bookmark->path, "/Music/Boards of Canada/Geogaddi/01 Ready Lets Go.flac");
    bookmark->position_ms = 754000u;
    bookmark->duration_ms = 1123000u;
    bookmark->track_id = 4711u;
    bookmark->saved_at = 1795483200u;
}

static void check_layout(void)
{
    struct helikon_bookmark bookmark;
    fill(&bookmark);
    /* Only the fields that matter here. */
    strcpy(bookmark.path, "/a.flac");
    bookmark.position_ms = 0u;
    bookmark.duration_ms = 0u;
    bookmark.track_id = 0u;
    bookmark.saved_at = 0u;

    char text[HELIKON_BOOKMARK_TEXT_MAX];
    size_t bytes = helikon_bookmark_write(&bookmark, text, sizeof(text));
    const char *expected =
        "helikon-bookmark 1\n"
        "path=/a.flac\n"
        "position_ms=0\n"
        "duration_ms=0\n"
        "track_id=0\n"
        "saved_at=0\n";
    if (expect(bytes == strlen(expected), "writer reports the exact length"))
        return;
    if (expect(memcmp(text, expected, bytes) == 0, "writer spells the record"))
        return;
    if (expect(text[bytes] == '\0', "writer terminates the record"))
        return;
}

static void check_roundtrip(void)
{
    struct helikon_bookmark bookmark;
    fill(&bookmark);
    char text[HELIKON_BOOKMARK_TEXT_MAX];
    size_t bytes = helikon_bookmark_write(&bookmark, text, sizeof(text));
    if (expect(bytes > 0u, "round trip writes"))
        return;

    struct helikon_bookmark back;
    memset(&back, 0xff, sizeof(back));
    if (expect(helikon_bookmark_parse(&back, text, bytes) == HELIKON_OK,
               "round trip parses"))
        return;
    if (expect(strcmp(back.path, bookmark.path) == 0, "round trip keeps the path"))
        return;
    if (expect(back.position_ms == bookmark.position_ms, "round trip keeps the position"))
        return;
    if (expect(back.duration_ms == bookmark.duration_ms, "round trip keeps the duration"))
        return;
    if (expect(back.track_id == bookmark.track_id, "round trip keeps the track id"))
        return;
    if (expect(back.saved_at == bookmark.saved_at, "round trip keeps the timestamp"))
        return;

    /* The extremes of the type survive: no sign, no overflow. */
    bookmark.position_ms = 0xffffffffu;
    bookmark.track_id = 1u;
    bytes = helikon_bookmark_write(&bookmark, text, sizeof(text));
    if (expect(bytes > 0u, "UINT32_MAX writes"))
        return;
    if (expect(helikon_bookmark_parse(&back, text, bytes) == HELIKON_OK &&
                   back.position_ms == 0xffffffffu,
               "UINT32_MAX survives the round trip"))
        return;

    /* Optional keys may be omitted; required ones may come in any order. */
    const char *minimal = "helikon-bookmark 1\n"
                          "position_ms=4200\n"
                          "path=/t.mp3\n";
    if (expect(helikon_bookmark_parse(&back, minimal, strlen(minimal)) ==
                   HELIKON_OK,
               "minimal record parses"))
        return;
    if (expect(back.position_ms == 4200u && strcmp(back.path, "/t.mp3") == 0 &&
                   back.duration_ms == 0u && back.track_id == 0u &&
                   back.saved_at == 0u,
               "omitted keys default to unknown"))
        return;
}

static void check_writer_refusals(void)
{
    struct helikon_bookmark bookmark;
    fill(&bookmark);
    char text[HELIKON_BOOKMARK_TEXT_MAX];

    strcpy(bookmark.path, "relative.flac");
    if (expect(helikon_bookmark_write(&bookmark, text, sizeof(text)) == 0u,
               "a relative path is not written"))
        return;

    strcpy(bookmark.path, "/");
    if (expect(helikon_bookmark_write(&bookmark, text, sizeof(text)) == 0u,
               "the root is not a track"))
        return;

    fill(&bookmark);
    bookmark.path[3] = '\n';
    if (expect(helikon_bookmark_write(&bookmark, text, sizeof(text)) == 0u,
               "a newline in a path is not written"))
        return;

    fill(&bookmark);
    bookmark.path[3] = '\t';
    if (expect(helikon_bookmark_write(&bookmark, text, sizeof(text)) == 0u,
               "a tab in a path is not written"))
        return;

    fill(&bookmark);
    memset(bookmark.path, 'a', sizeof(bookmark.path));
    bookmark.path[0] = '/';
    if (expect(helikon_bookmark_write(&bookmark, text, sizeof(text)) == 0u,
               "a path with no terminator is not written"))
        return;

    /* A buffer too small for the whole record is refused, never truncated. */
    fill(&bookmark);
    if (expect(helikon_bookmark_write(&bookmark, text, 16u) == 0u,
               "a short buffer is refused"))
        return;
    if (expect(text[0] == '\0', "a refused write leaves no half record"))
        return;
    if (expect(helikon_bookmark_write(NULL, text, sizeof(text)) == 0u,
               "a null bookmark is refused"))
        return;
}

/* Parses `text` and expects HELIKON_ERR_FORMAT, with the record zeroed. */
static int refuses(const char *what, const char *text)
{
    struct helikon_bookmark bookmark;
    int rc = helikon_bookmark_parse(&bookmark, text, strlen(text));
    if (expect(rc == HELIKON_ERR_FORMAT, what))
        return 1;
    for (size_t index = 0u; index < sizeof(bookmark); ++index) {
        if (((const unsigned char *)&bookmark)[index] != 0u) {
            if (expect(0, "a refused record is zeroed"))
                return 1;
            break;
        }
    }
    return 0;
}

static void check_parse_refusals(void)
{
    refuses("empty input is refused", "");
    refuses("a missing magic is refused", "path=/a.flac\nposition_ms=1\n");
    refuses("a wrong version is refused",
            "helikon-bookmark 2\npath=/a.flac\nposition_ms=1\n");
    refuses("a magic only file is refused", "helikon-bookmark 1\n");
    refuses("a missing path is refused", "helikon-bookmark 1\nposition_ms=1\n");
    refuses("a missing position is refused",
            "helikon-bookmark 1\npath=/a.flac\n");
    refuses("an unknown key is refused",
            "helikon-bookmark 1\npath=/a.flac\nposition_ms=1\nvolume=90\n");
    refuses("a duplicate key is refused",
            "helikon-bookmark 1\npath=/a.flac\npath=/b.flac\nposition_ms=1\n");
    refuses("a CR is refused",
            "helikon-bookmark 1\r\npath=/a.flac\r\nposition_ms=1\r\n");
    refuses("a missing final newline is refused",
            "helikon-bookmark 1\npath=/a.flac\nposition_ms=1");
    refuses("an empty line is refused",
            "helikon-bookmark 1\npath=/a.flac\n\nposition_ms=1\n");
    refuses("a line without a separator is refused",
            "helikon-bookmark 1\npath=/a.flac\ngarbage\nposition_ms=1\n");
    refuses("an empty value is refused",
            "helikon-bookmark 1\npath=/a.flac\nposition_ms=\n");
    refuses("a relative path is refused",
            "helikon-bookmark 1\npath=a.flac\nposition_ms=1\n");
    refuses("a signed number is refused",
            "helikon-bookmark 1\npath=/a.flac\nposition_ms=+1\n");
    refuses("a negative number is refused",
            "helikon-bookmark 1\npath=/a.flac\nposition_ms=-1\n");
    refuses("a number with trailing text is refused",
            "helikon-bookmark 1\npath=/a.flac\nposition_ms=1s\n");
    refuses("an overflowing number is refused",
            "helikon-bookmark 1\npath=/a.flac\nposition_ms=4294967296\n");
    refuses("a control byte in a path is refused",
            "helikon-bookmark 1\npath=/a\x01\x02.flac\nposition_ms=1\n");
    /* A NUL in the middle is refused even when the length says otherwise. */
    char with_nul[] = "helikon-bookmark 1\npath=/a.flac\nposition_ms=1\n";
    struct helikon_bookmark bookmark;
    if (expect(helikon_bookmark_parse(&bookmark, with_nul, sizeof(with_nul)) ==
                   HELIKON_ERR_FORMAT,
               "a trailing zero byte is refused"))
        return;

    /* A path exactly at the limit is refused; one shorter is accepted. */
    char text[HELIKON_BOOKMARK_TEXT_MAX];
    size_t at = 0u;
    const char *magic = "helikon-bookmark 1\npath=";
    for (size_t index = 0u; index < strlen(magic); ++index)
        text[at++] = magic[index];
    for (size_t index = 0u; index < HELIKON_BOOKMARK_PATH_MAX; ++index) {
        if (index == 0u)
            text[at++] = '/';
        else if (index + 1u == HELIKON_BOOKMARK_PATH_MAX)
            text[at++] = 'a'; /* exactly PATH_MAX bytes, no terminator */
        else
            text[at++] = 'b';
    }
    const char *tail = "\nposition_ms=1\n";
    for (size_t index = 0u; index < strlen(tail); ++index)
        text[at++] = tail[index];
    if (expect(helikon_bookmark_parse(&bookmark, text, at) ==
                   HELIKON_ERR_FORMAT,
               "a path at the limit is refused"))
        return;

    /* A file over the module's limit is refused without being read. */
    char big[HELIKON_BOOKMARK_TEXT_MAX + 1u];
    memset(big, 'a', sizeof(big));
    if (expect(helikon_bookmark_parse(&bookmark, big, sizeof(big)) ==
                   HELIKON_ERR_FORMAT,
               "an oversized record is refused"))
        return;
}

int main(void)
{
    check_layout();
    check_roundtrip();
    check_writer_refusals();
    check_parse_refusals();

    if (failures != 0) {
        printf("FAIL bookmark: %d failure(s)\n", failures);
        return 1;
    }
    printf("PASS bookmark: exact bytes, round trip, writer and parser refusals\n");
    return 0;
}
