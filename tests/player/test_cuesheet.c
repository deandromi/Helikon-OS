/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Host tests for the cue sheet reader (T-093, the album half).
 *
 * A cue sheet says where each track of one audio file starts. Everything in it
 * is a claim, and a wrong frame offset is audible: the track starts in the
 * middle of the previous one. Each test below is one of the rules stated in
 * `cuesheet.h`, plus the arithmetic the player will do with the result.
 */
#include "cuesheet.h"

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

static int parse(struct helikon_cuesheet *cue, const char *text)
{
    return helikon_cuesheet_parse(cue, text, strlen(text));
}

static int track_is(const struct helikon_cuesheet *cue, uint16_t index,
                    uint16_t number, uint32_t start_frame)
{
    return cue->tracks[index].number == number &&
           cue->tracks[index].start_frame == start_frame;
}

static int title_is(const struct helikon_cuesheet *cue, uint16_t index,
                    const char *title)
{
    const struct helikon_cuesheet_track *track = &cue->tracks[index];
    if (track->title == NULL || track->title_length == 0u)
        return 0;
    size_t length = strlen(title);
    return track->title_length == length &&
           memcmp(track->title, title, length) == 0;
}

static int file_is(const struct helikon_cuesheet *cue, uint16_t index,
                   const char *path)
{
    if (index >= cue->file_count)
        return 0;
    size_t length = strlen(path);
    return cue->files[index].path_length == length &&
           memcmp(cue->files[index].path, path, length) == 0;
}

/* A one-file album with three tracks, which is the shape of most cue sheets. */
static void test_single_file_album(void)
{
    static const char text[] =
        "REM GENRE Ambient\n"
        "PERFORMER \"The Ember Set\"\n"
        "TITLE \"Night Harbour\"\n"
        "FILE \"Night Harbour.flac\" WAVE\n"
        "  TRACK 01 AUDIO\n"
        "    TITLE \"First Light\"\n"
        "    PERFORMER \"The Ember Set\"\n"
        "    INDEX 01 00:00:00\n"
        "  TRACK 02 AUDIO\n"
        "    TITLE \"Second Light\"\n"
        "    INDEX 01 03:30:00\n"
        "  TRACK 03 AUDIO\n"
        "    TITLE \"Tide\"\n"
        "    INDEX 01 07:00:37\n";

    struct helikon_cuesheet cue;
    expect(parse(&cue, text) == HELIKON_OK, "a plain album parses");
    expect(cue.refused == 0u, "a plain album refuses nothing");
    expect(cue.complete == 1u, "a plain album is complete");
    expect(cue.file_count == 1u, "one file");
    expect(file_is(&cue, 0, "Night Harbour.flac"), "the file path is unquoted");
    expect(cue.track_count == 3u, "three tracks");
    expect(track_is(&cue, 0u, 1u, 0u), "track 1 starts at frame 0");
    expect(track_is(&cue, 1u, 2u, (3u * 60u + 30u) * 75u),
           "track 2 starts at 3:30");
    expect(track_is(&cue, 2u, 3u, 7u * 60u * 75u + 37u),
           "track 3 starts at 7:00:37");
    expect(title_is(&cue, 0u, "First Light"), "track 1 title");
    expect(title_is(&cue, 2u, "Tide"), "track 3 title");
    expect(cue.title_length == strlen("Night Harbour") &&
               memcmp(cue.title, "Night Harbour", cue.title_length) == 0,
           "the album title");
    expect(cue.performer_length == strlen("The Ember Set") &&
               memcmp(cue.performer, "The Ember Set", cue.performer_length) == 0,
           "the album performer");
    expect(cue.tracks[0].pregap_frame == 0u,
           "a track without INDEX 00 has no pregap");
    expect(cue.tracks[0].file_index == 0u, "the track belongs to file 0");
}

/* Pregap, sub-indexes and a file that follows them all. */
static void test_pregap_and_subindex(void)
{
    static const char text[] =
        "FILE \"a.flac\" WAVE\n"
        "TRACK 01 AUDIO\n"
        "INDEX 00 00:00:00\n"
        "INDEX 01 00:02:00\n"
        "TRACK 02 AUDIO\n"
        "INDEX 00 03:00:00\n"
        "INDEX 01 03:02:00\n"
        "INDEX 02 03:10:00\n";

    struct helikon_cuesheet cue;
    expect(parse(&cue, text) == HELIKON_OK, "pregap and sub-indexes parse");
    expect(cue.refused == 0u, "a pregap is not a refusal");
    expect(cue.track_count == 2u, "two tracks");
    expect(track_is(&cue, 1u, 2u, (3u * 60u + 2u) * 75u),
           "INDEX 01 is the start, not INDEX 02");
    expect(cue.tracks[0].pregap_frame == 0u, "track 1 starts with its pregap");
    expect(cue.tracks[1].pregap_frame == 3u * 60u * 75u,
           "track 2's INDEX 00 is its pregap");

    /*
     * A pregap after its own start cannot be one: the track would begin before
     * the silence that precedes it. The start is kept and the sheet is marked
     * incomplete rather than the track being dropped.
     */
    static const char backwards[] =
        "FILE \"a.flac\" WAVE\n"
        "TRACK 01 AUDIO\n"
        "INDEX 01 00:10:00\n"
        "INDEX 00 00:20:00\n";
    expect(parse(&cue, backwards) == HELIKON_OK, "a backwards pregap still parses");
    expect(cue.track_count == 1u, "the track survives a backwards pregap");
    expect(cue.refused == 1u, "the backwards pregap is refused");
    expect(cue.complete == 0u, "the sheet is incomplete");
    expect(cue.tracks[0].pregap_frame == cue.tracks[0].start_frame,
           "the pregap is clamped to the start");
}

/* Times that cannot exist are refused, not rounded. */
static void test_bad_times(void)
{
    static const char *const bad[] = {
        "FILE \"a.flac\" WAVE\nTRACK 01 AUDIO\nINDEX 01 00:60:00\n",
        "FILE \"a.flac\" WAVE\nTRACK 01 AUDIO\nINDEX 01 00:00:75\n",
        "FILE \"a.flac\" WAVE\nTRACK 01 AUDIO\nINDEX 01 00:00\n",
        "FILE \"a.flac\" WAVE\nTRACK 01 AUDIO\nINDEX 01 00:00:00:00\n",
        "FILE \"a.flac\" WAVE\nTRACK 01 AUDIO\nINDEX 01 aa:00:00\n",
        "FILE \"a.flac\" WAVE\nTRACK 01 AUDIO\nINDEX 01 999999999999:00:00\n",
        "FILE \"a.flac\" WAVE\nTRACK 01 AUDIO\nINDEX 01 -1:00:00\n",
        "FILE \"a.flac\" WAVE\nTRACK 01 AUDIO\nINDEX 01\n",
        ("FILE \"a.flac\" WAVE\nTRACK 01 AUDIO\nINDEX 01 00:00:00\nTRACK 02 AUDIO\n"
         "INDEX 01 00:00:01\nTRACK 03 AUDIO\nINDEX 01 00:00:00\n"),
    };
    struct helikon_cuesheet cue;
    for (size_t index = 0u; index < sizeof(bad) / sizeof(bad[0]); ++index) {
        int rc = parse(&cue, bad[index]);
        /* A sheet whose only track has no usable start has nothing to play. */
        int expected = index == 0u || index == 1u || index == 2u ||
                               index == 3u || index == 4u || index == 5u ||
                               index == 6u || index == 7u
                           ? HELIKON_ERR_FORMAT
                           : HELIKON_OK;
        expect(rc == expected, "a time that cannot exist is refused");
        expect(cue.complete == 0u, "and the sheet is marked incomplete");
    }

    /* The one that parses: the third track goes backwards, so it is dropped. */
    expect(parse(&cue, bad[8]) == HELIKON_OK, "a backwards track parses");
    expect(cue.track_count == 2u, "the backwards track is dropped");
    expect(cue.refused == 1u, "and counted");
}

/* Track numbering rules. */
static void test_track_numbers(void)
{
    static const char duplicate[] =
        "FILE \"a.flac\" WAVE\n"
        "TRACK 01 AUDIO\nINDEX 01 00:00:00\n"
        "TRACK 01 AUDIO\nINDEX 01 00:01:00\n"
        "TRACK 03 AUDIO\nINDEX 01 00:02:00\n";
    struct helikon_cuesheet cue;
    expect(parse(&cue, duplicate) == HELIKON_OK, "a duplicate track number parses");
    expect(cue.track_count == 2u, "the duplicate is dropped");
    expect(cue.refused == 1u, "the duplicate is counted");

    static const char out_of_range[] =
        "FILE \"a.flac\" WAVE\n"
        "TRACK 00 AUDIO\n"
        "TRACK 100 AUDIO\n"
        "TRACK 01 AUDIO\nINDEX 01 00:00:00\n";
    expect(parse(&cue, out_of_range) == HELIKON_OK, "out-of-range numbers parse");
    expect(cue.track_count == 1u, "only the valid track survives");
    expect(cue.refused == 2u, "both out-of-range numbers are counted");

    /* A TRACK before any FILE has offsets relative to nothing. */
    static const char no_file[] =
        "TRACK 01 AUDIO\n"
        "INDEX 01 00:00:00\n"
        "FILE \"a.flac\" WAVE\n"
        "TRACK 02 AUDIO\nINDEX 01 00:00:00\n";
    expect(parse(&cue, no_file) == HELIKON_OK, "a TRACK before FILE parses");
    expect(cue.track_count == 1u, "the track before FILE is refused");
    expect(cue.tracks[0].number == 2u, "the track after FILE survives");
    expect(cue.refused == 1u, "and the refusal is counted");

    /* 99 tracks is a whole disc; the hundredth cannot exist. */
    static char many[99u * 64u];
    size_t used = 0u;
    used += (size_t)snprintf(many + used, sizeof(many) - used,
                             "FILE \"a.flac\" WAVE\n");
    for (unsigned int number = 1u; number <= 99u; ++number) {
        unsigned int seconds = (number - 1u) * 2u;
        used += (size_t)snprintf(many + used, sizeof(many) - used,
                                 "TRACK %02u AUDIO\nINDEX 01 %02u:%02u:00\n",
                                 number, seconds / 60u, seconds % 60u);
    }
    expect(parse(&cue, many) == HELIKON_OK, "99 tracks parse");
    expect(cue.track_count == 99u, "a whole disc fits");
    expect(cue.refused == 0u, "nothing refused");
}

/* One file per track, which is what a hand-built set looks like. */
static void test_multiple_files(void)
{
    static const char text[] =
        "FILE \"01 First Light.flac\" WAVE\n"
        "TRACK 01 AUDIO\nINDEX 01 00:00:00\n"
        "FILE \"02 Second Light.flac\" WAVE\n"
        "TRACK 02 AUDIO\nINDEX 01 00:00:00\n"
        "FILE \"03 Tide.flac\" WAVE\n"
        "TRACK 03 AUDIO\nINDEX 01 00:00:00\n";

    struct helikon_cuesheet cue;
    expect(parse(&cue, text) == HELIKON_OK, "three files parse");
    expect(cue.file_count == 3u, "three files are counted");
    expect(file_is(&cue, 1u, "02 Second Light.flac"), "the second file path");
    expect(cue.tracks[2].file_index == 2u, "track 3 belongs to file 2");
    expect(cue.tracks[2].start_frame == 0u, "each file starts at zero");
    expect(cue.complete == 1u, "multiple files are complete");
}

/* Capitals, CRLF, BOM, quotes and the keys nobody plays. */
static void test_formatting(void)
{
    static const char text[] =
        "\xEF\xBB\xBFREM COMMENT \"ripped\"\r\n"
        "catalog 1234567890123\r\n"
        "file \"a.flac\" wave\r\n"
        "track 01 audio\r\n"
        "title First Light\r\n"
        "index 01 00:00:00\r\n"
        "Track 02 AUDIO\r\n"
        "TITLE \"Second \"Light\"\"\r\n"
        "INDEX 01 00:01:00\r\n"
        "FLAGS DCP\r\n"
        "ISRC ABCDE1234567\r\n";

    struct helikon_cuesheet cue;
    expect(parse(&cue, text) == HELIKON_OK, "mixed case and CRLF parse");
    expect(cue.file_count == 1u, "the lower-case file key is read");
    expect(cue.track_count == 2u, "both tracks are read");
    expect(title_is(&cue, 0u, "First Light"), "an unquoted title is taken whole");
    expect(title_is(&cue, 1u, "Second \"Light\""),
           "a quoted title keeps its inner quotes");
    expect(cue.refused == 0u, "nothing refused");

    /* Leading and trailing whitespace around a value is not part of it. */
    static const char spaces[] =
        "FILE   \"a.flac\"   WAVE   \n"
        "TRACK   01   AUDIO\n"
        "TITLE\t  Spaced  \t\n"
        "INDEX\t01\t00:00:10\t\n";
    expect(parse(&cue, spaces) == HELIKON_OK, "tabs and padding parse");
    expect(title_is(&cue, 0u, "Spaced"), "padding is trimmed");
    expect(track_is(&cue, 0u, 1u, 10u), "the tabbed index reads");
}

/* Inputs that are not cue sheets at all. */
static void test_rejections(void)
{
    struct helikon_cuesheet cue;
    expect(helikon_cuesheet_parse(&cue, NULL, 0u) == HELIKON_ERR_ARGUMENT &&
               helikon_cuesheet_parse(&cue, NULL, 10u) == HELIKON_ERR_ARGUMENT,
           "a null buffer is an argument error");
    expect(helikon_cuesheet_parse(NULL, "x", 1u) == HELIKON_ERR_ARGUMENT,
           "a null cuesheet is an argument error");
    expect(helikon_cuesheet_parse(&cue, "", 0u) == HELIKON_ERR_FORMAT,
           "empty text is FORMAT");
    expect(parse(&cue, "REM nothing here\nCATALOG 1234\n") == HELIKON_ERR_FORMAT,
           "a sheet with no track is FORMAT");
    expect(parse(&cue, "FILE \"a.flac\" WAVE\nTRACK 01 AUDIO\nTITLE \"x\"\n") ==
               HELIKON_ERR_FORMAT,
           "a track with no INDEX 01 is not playable");
    expect(cue.refused == 1u, "and the track that was dropped is counted");

    /* A NUL means the caller's length and the file disagree. */
    static const char with_nul[] =
        "FILE \"a.flac\" WAVE\nTRACK 01 AUDIO\nINDEX 01 00:00:00\n";
    expect(helikon_cuesheet_parse(&cue, with_nul, sizeof(with_nul) - 1u) ==
               HELIKON_OK,
           "the same text without the terminator parses");
    expect(helikon_cuesheet_parse(&cue, with_nul, sizeof(with_nul)) ==
               HELIKON_ERR_FORMAT,
           "a NUL inside the length is FORMAT");
}

/* The arithmetic and the lookup the player uses. */
static void test_helpers(void)
{
    expect(helikon_cuesheet_frame_to_ms(0u) == 0u, "frame 0 is 0 ms");
    expect(helikon_cuesheet_frame_to_ms(75u) == 1000u, "75 frames is one second");
    expect(helikon_cuesheet_frame_to_ms(150u) == 2000u, "150 frames is two");
    expect(helikon_cuesheet_frame_to_ms(1u) == 13u, "a frame rounds down to 13 ms");
    expect(helikon_cuesheet_frame_to_ms(74u) == 986u, "74 frames is 986 ms");
    expect(helikon_cuesheet_frame_to_ms(75u * 60u * 99u) == 99u * 60000u,
           "99 minutes is 5,940,000 ms");

    static const char text[] =
        "FILE \"a.flac\" WAVE\n"
        "TRACK 01 AUDIO\nINDEX 01 00:00:00\n"
        "TRACK 02 AUDIO\nINDEX 01 00:01:00\n"
        "TRACK 05 AUDIO\nINDEX 01 00:02:00\n";
    struct helikon_cuesheet cue;
    expect(parse(&cue, text) == HELIKON_OK, "the lookup sheet parses");
    uint16_t at = 0xFFFFu;
    expect(helikon_cuesheet_track_by_number(&cue, 2u, &at) == HELIKON_OK &&
               at == 1u,
           "track 2 is at array position 1");
    expect(helikon_cuesheet_track_by_number(&cue, 3u, &at) ==
               HELIKON_ERR_NOT_FOUND,
           "a number the sheet skips is NOT_FOUND");
    expect(helikon_cuesheet_track_by_number(&cue, 5u, &at) == HELIKON_OK &&
               at == 2u,
           "a number the sheet jumps to still resolves");
    expect(helikon_cuesheet_track_by_number(NULL, 1u, &at) ==
               HELIKON_ERR_ARGUMENT,
           "a null cuesheet is an argument error");
    expect(helikon_cuesheet_track_by_number(&cue, 1u, NULL) ==
               HELIKON_ERR_ARGUMENT,
           "a null output is an argument error");
}

/* A long line is refused, and the rest of the sheet still plays. */
static void test_limits(void)
{
    static char text[2048];
    size_t used = 0u;
    used += (size_t)snprintf(text + used, sizeof(text) - used,
                             "FILE \"a.flac\" WAVE\n");
    used += (size_t)snprintf(text + used, sizeof(text) - used, "TITLE ");
    while (used < HELIKON_CUESHEET_LINE_MAX + 100u && used < sizeof(text) - 2u)
        text[used++] = 'x';
    text[used++] = '\n';
    used += (size_t)snprintf(text + used, sizeof(text) - used,
                             "TRACK 01 AUDIO\nINDEX 01 00:00:00\n");
    text[used] = '\0';

    struct helikon_cuesheet cue;
    expect(parse(&cue, text) == HELIKON_OK, "a sheet with one huge line parses");
    expect(cue.track_count == 1u, "the track after it survives");
    expect(cue.refused == 1u, "the huge line is refused");
}

int main(void)
{
    test_single_file_album();
    test_pregap_and_subindex();
    test_bad_times();
    test_track_numbers();
    test_multiple_files();
    test_formatting();
    test_rejections();
    test_helpers();
    test_limits();

    if (failures != 0u) {
        printf("%u cue sheet failure(s)\n", failures);
        return 1;
    }
    printf("PASS cuesheet: album, pregap, times, numbering, files, formatting, "
           "rejections, helpers, limits\n");
    return 0;
}
