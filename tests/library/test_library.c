/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Host test: HelikonDB read from a real HFS+ volume through the read-only VFS.
 *
 * The image is created by `run-library-macos.sh` with `hdiutil` (journaled HFS+,
 * the same on-disk shape as the device) and holds `/.helikon/music.hdb` built by
 * `tools/hdb_build.py fixture`, the six files the fixture's catalog points at,
 * one `/Music/first.mp3` that only the storage test uses, and a playlist under
 * `/Playlists`. This proves the storage path end to end on the host: mount ->
 * path lookup -> open -> seek/read -> HDB records, then a playlist file
 * resolved against the catalog into a queue, and the first queued track played
 * from the volume. It is not a device run and makes no claim about the 6G.
 */
#include "hdb_vfs.h"

#include <stdio.h>
#include <string.h>

#include "audio.h"
#include "helikon_error.h"
#include "library.h"
#include "music_source.h"
#include "player.h"
#include "read_ahead.h"
#include "vfs.h"
#include "vfs_media.h"

static FILE *image;
static long image_size;

static int failures;

static void check(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL %s\n", message);
        failures += 1;
    }
}

/* hfs_read_fn over the image file: absolute offsets, exact lengths. */
static int image_read(void *context, uint64_t offset, void *buffer,
                      size_t length)
{
    (void)context;
    if (image == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (offset > (uint64_t)image_size ||
        length > (size_t)((uint64_t)image_size - offset))
        return HELIKON_ERR_IO;
    if (fseek(image, (long)offset, SEEK_SET) != 0)
        return HELIKON_ERR_IO;
    if (fread(buffer, 1u, length, image) != length)
        return HELIKON_ERR_IO;
    return HELIKON_OK;
}

/*
 * A silent audio HAL for the host. The player only needs the ring to accept
 * PCM and to advance its position; there is no DMA, I2S or PMU on this side, and
 * nothing here pretends otherwise. The captured PCM source is how the test
 * compares two decodes frame by frame.
 */
static helikon_audio_pcm_source captured_source;
static void *captured_context;
static size_t captured_frames;

int helikon_audio_init(struct helikon_audio *audio, struct helikon_pmu *pmu,
                       struct helikon_clock *clock, uint32_t timeout_ms)
{
    (void)pmu;
    (void)clock;
    (void)timeout_ms;
    memset(audio, 0, sizeof(*audio));
    audio->initialized = 1;
    return HELIKON_OK;
}

int helikon_audio_set_rate(struct helikon_audio *audio, uint32_t sample_rate)
{
    audio->sample_rate = sample_rate;
    return (sample_rate == 44100u || sample_rate == 48000u) ? HELIKON_OK
                                                            : HELIKON_ERR_UNSUPPORTED;
}

int helikon_audio_stream_start(struct helikon_audio *audio,
                               const int16_t *pcm, size_t frames,
                               uint32_t channels)
{
    (void)pcm;
    (void)channels;
    audio->stream_frames = frames;
    audio->stream_played = 0u;
    audio->stream_started = 1;
    audio->running = 1;
    return HELIKON_OK;
}

int helikon_audio_stream_start_source(struct helikon_audio *audio,
                                      helikon_audio_pcm_source source,
                                      void *context, size_t frames)
{
    audio->stream_source = source;
    audio->stream_source_context = context;
    audio->stream_frames = frames;
    audio->stream_played = 0u;
    audio->stream_started = 1;
    audio->running = 1;
    captured_source = source;
    captured_context = context;
    captured_frames = frames;
    return HELIKON_OK;
}

int helikon_audio_stream_service(struct helikon_audio *audio)
{
    /* One DMA-sized window per call, like the ring with four buffers. */
    size_t remaining = audio->stream_frames - audio->stream_played;
    size_t add = remaining < HELIKON_AUDIO_RING_FRAMES
                     ? remaining
                     : HELIKON_AUDIO_RING_FRAMES;
    audio->stream_played += add;
    return HELIKON_OK;
}

int helikon_audio_stream_pause(struct helikon_audio *audio)
{
    audio->paused = 1;
    return HELIKON_OK;
}

int helikon_audio_stream_resume(struct helikon_audio *audio)
{
    audio->paused = 0;
    audio->running = 1;
    return HELIKON_OK;
}

void helikon_audio_stream_stop(struct helikon_audio *audio)
{
    audio->stream_started = 0;
    audio->running = 0;
    audio->paused = 0;
}

int helikon_audio_stream_finished(const struct helikon_audio *audio)
{
    return audio->stream_started && audio->stream_played >= audio->stream_frames;
}

uint32_t helikon_audio_stream_position_frames(const struct helikon_audio *audio)
{
    return (uint32_t)audio->stream_played;
}

uint32_t helikon_audio_stream_underruns(const struct helikon_audio *audio)
{
    (void)audio;
    return 0u;
}

void helikon_audio_peak_levels(const struct helikon_audio *audio,
                               uint8_t *levels, uint32_t count)
{
    (void)audio;
    if (levels == NULL)
        return;
    for (uint32_t index = 0u; index < count; ++index)
        levels[index] = 0u;
}

/* Pull frames from the ring's source until `want` or end of stream. */
static size_t pull_frames(helikon_audio_pcm_source source, void *context,
                          int16_t *pcm, size_t want)
{
    size_t total = 0u;
    while (total < want) {
        size_t got = 0u;
        if (source(context, pcm + total * 2u, want - total, &got) != HELIKON_OK ||
            got == 0u)
            break;
        total += got;
    }
    return total;
}

/*
 * The whole point of the exercise: an audio file that exists only on the HFS+
 * volume, opened through the VFS, wrapped in the read-ahead window and decoded
 * by the player, must produce the same PCM as the same bytes decoded from a
 * resident buffer. A mismatch means the storage path lost or invented audio.
 */
static void test_storage_playback(struct vfs *vfs)
{
    static const char *const path = "/Music/first.mp3";
    struct vfs_entry entry;
    check(vfs_stat(vfs, path, &entry) == HELIKON_OK, "stat the audio file");
    if (entry.size == 0u) {
        check(0, "the audio file is not empty");
        return;
    }

    struct vfs_file_handle handle;
    check(vfs_file_open(vfs, path, &handle) == HELIKON_OK,
          "open the audio file through the VFS");

    struct helikon_vfs_media media;
    struct helikon_codec_source raw_source;
    check(helikon_vfs_media_init(&media, vfs, handle, entry.size, &raw_source) ==
              HELIKON_OK,
          "bind the file to a codec source");

    static uint8_t window[HELIKON_READ_AHEAD_BYTES];
    struct helikon_read_ahead read_ahead;
    struct helikon_codec_source source;
    check(helikon_read_ahead_init(&read_ahead, window, sizeof(window),
                                  &raw_source, &source) == HELIKON_OK,
          "wrap the source in the read-ahead window");

    struct helikon_pmu pmu;
    struct helikon_clock clock;
    memset(&pmu, 0, sizeof(pmu));
    memset(&clock, 0, sizeof(clock));

    static struct helikon_player player;
    check(helikon_player_open_media(&player, &source, &pmu, &clock, 2000u) ==
              HELIKON_OK,
          "the player opens audio that lives on the volume");
    check(helikon_player_duration_ms(&player) > 0u,
          "the player reports a duration from the volume");

    /* The ring starts on the first chunk, as it does on the device. */
    check(helikon_player_play_chunk(&player, 2047u) == 0,
          "start playback from the volume");
    check(captured_source != NULL, "the HAL received a stream source");

    /* Pull real PCM out of the ring's source, the way the DMA would. */
    static int16_t stored_pcm[8192];
    size_t want = 2048u;
    size_t stored_frames =
        pull_frames(captured_source, captured_context, stored_pcm, want);
    check(stored_frames != 0u, "the volume produced audio frames");
    check(stored_frames <= want, "the volume did not produce more than asked");

    uint64_t stored_energy = 0u;
    for (size_t index = 0u; index < stored_frames * 2u; ++index)
        stored_energy += (uint64_t)(stored_pcm[index] < 0
                                        ? -(int32_t)stored_pcm[index]
                                        : (int32_t)stored_pcm[index]);
    check(stored_energy != 0u, "the decoded audio is not silence");
    check(read_ahead.stats.bytes_read > 0u &&
              read_ahead.stats.bytes_read <= entry.size,
          "the window read the file, not more");
    check(read_ahead.stats.refills != 0u, "the window filled from the VFS");

    /* The same file, resident: the reference decode. */
    static uint8_t resident[262144];
    check(entry.size <= sizeof(resident), "the fixture fits the resident buffer");
    size_t loaded = 0u;
    uint64_t position = 1u;
    check(vfs_file_seek(vfs, handle, VFS_SEEK_SET, 0, &position) == HELIKON_OK &&
              position == 0u,
          "rewind the file");
    check(vfs_file_read(vfs, handle, resident, (size_t)entry.size, &loaded) ==
              HELIKON_OK &&
              loaded == (size_t)entry.size,
          "read the whole file through the VFS");

    static struct helikon_player reference;
    check(helikon_player_open(&reference, resident, loaded, &pmu, &clock,
                              2000u) == HELIKON_OK,
          "the resident path opens the same bytes");
    check(helikon_player_duration_ms(&reference) ==
              helikon_player_duration_ms(&player),
          "both paths agree on the duration");
    check(helikon_player_play_chunk(&reference, 2047u) == 0,
          "start the reference stream");
    check(captured_source != NULL, "the reference decode started a stream");
    static int16_t resident_pcm[8192];
    size_t resident_frames =
        pull_frames(captured_source, captured_context, resident_pcm, want);
    check(resident_frames == stored_frames,
          "both paths decode the same number of frames");
    check(memcmp(stored_pcm, resident_pcm,
                 stored_frames * sizeof(int16_t) * 2u) == 0,
          "storage and buffer decode to the same PCM");

    helikon_player_stop(&player);
    helikon_player_stop(&reference);
    check(vfs_file_close(vfs, handle) == HELIKON_OK,
          "close the audio file");
}

/* Open a catalog path through the VFS, play it, and return the peak amplitude of
 * the PCM it produced. Zero means the volume gave us silence or nothing. */
static uint64_t play_catalog_path(struct vfs *vfs, const char *path)
{
    struct vfs_entry entry;
    if (vfs_stat(vfs, path, &entry) != HELIKON_OK || entry.size == 0u)
        return 0u;

    struct vfs_file_handle handle;
    if (vfs_file_open(vfs, path, &handle) != HELIKON_OK)
        return 0u;

    struct helikon_vfs_media media;
    struct helikon_codec_source raw_source;
    if (helikon_vfs_media_init(&media, vfs, handle, entry.size, &raw_source) !=
        HELIKON_OK) {
        (void)vfs_file_close(vfs, handle);
        return 0u;
    }

    struct helikon_pmu pmu;
    struct helikon_clock clock;
    memset(&pmu, 0, sizeof(pmu));
    memset(&clock, 0, sizeof(clock));

    static struct helikon_player player;
    uint64_t energy = 0u;
    static int16_t pcm[8192];
    captured_source = NULL;
    if (helikon_player_open_media(&player, &raw_source, &pmu, &clock, 2000u) ==
            HELIKON_OK &&
        helikon_player_play_chunk(&player, 2047u) == 0 && captured_source != NULL) {
        size_t frames = pull_frames(captured_source, captured_context, pcm, 2048u);
        for (size_t index = 0u; index < frames * 2u; ++index)
            energy += (uint64_t)(pcm[index] < 0 ? -(int32_t)pcm[index]
                                                : (int32_t)pcm[index]);
    }
    helikon_player_stop(&player);
    (void)vfs_file_close(vfs, handle);
    return energy;
}

/*
 * The path a user takes, end to end on the host: a playlist file that lives on
 * the volume, the catalog read through the VFS, the index built from it, a queue
 * in playlist order, and the first queued track played from the volume.
 *
 * The playlist names the catalog's own paths — one of them relative with `..`,
 * one absolute, one that is not in the catalog and one that escapes the root —
 * so resolution can only succeed if the index agrees with the catalog byte for
 * byte, and the playback can only produce audio if that catalog path is also a
 * real file on the volume.
 */
static void test_playlist_queue(struct vfs *vfs, const struct hdb *db)
{
    static const char *const playlist = "/Playlists/first.m3u";
    struct vfs_entry entry;
    check(vfs_stat(vfs, playlist, &entry) == HELIKON_OK, "stat the playlist");
    check(entry.kind == VFS_KIND_FILE, "the playlist is a file");
    if (entry.kind != VFS_KIND_FILE || entry.size == 0u || entry.size >= 4096u) {
        check(0, "the playlist fits the buffer");
        return;
    }

    struct vfs_file_handle handle;
    check(vfs_file_open(vfs, playlist, &handle) == HELIKON_OK,
          "open the playlist through the VFS");
    static char text[4096];
    size_t loaded = 0u;
    check(vfs_file_read(vfs, handle, text, (size_t)entry.size, &loaded) ==
              HELIKON_OK &&
              loaded == (size_t)entry.size,
          "read the playlist through the VFS");
    check(vfs_file_close(vfs, handle) == HELIKON_OK, "close the playlist");
    text[loaded] = '\0';

    /* 1024 hashes plus the 16 KiB, so it lives in .bss, not on the stack. */
    static struct helikon_library_index index;
    check(helikon_library_index_build(&index, db) == HELIKON_OK,
          "index the catalog read through the VFS");
    check(index.used == 6u, "the index holds six tracks");
    check(index.complete == 1u, "every fixture track was indexed");

    /* First principle: a catalog path must find the track it came from. */
    for (uint32_t track_index = 0u; track_index < db->track_count; ++track_index) {
        struct hdb_track track;
        char track_path[HELIKON_LIBRARY_PATH_MAX];
        uint32_t length = 0u;
        check(hdb_track(db, track_index, &track) == HELIKON_OK,
              "a track record reads");
        check(hdb_string(db, track.path_off, track_path, sizeof(track_path),
                         &length) == HELIKON_OK,
              "a track path reads");
        uint32_t found = 0xFFFFFFFFu;
        check(helikon_library_index_find(&index, track_path, &found) ==
                  HELIKON_OK,
              "a catalog path resolves through the index");
        check(found == track_index, "and resolves to its own track");
    }

    /* A path the catalog does not have is NOT_FOUND, not a near miss. */
    uint32_t nowhere = 0u;
    check(helikon_library_index_find(&index, "/Music/Nowhere/Gone.m4a",
                                     &nowhere) == HELIKON_ERR_NOT_FOUND,
          "an absent path is NOT_FOUND");

    static struct helikon_queue queue;
    struct helikon_playlist_queue_result result;
    check(helikon_library_queue_from_playlist(&index, &queue, text, loaded,
                                              "/Playlists", &result) ==
              HELIKON_OK,
          "resolve the playlist into the queue");
    check(result.entries == 4u, "four entries came out of the parser");
    check(result.added == 3u, "three entries resolved against the catalog");
    check(result.missing == 1u, "one entry is not in the catalog");
    check(result.refused == 1u, "one entry escaped the root and was refused");
    check(queue.count == 3u, "the queue holds the three resolved tracks");

    /* The playlist order is Tide (5), Signal (2), First Light (0): nothing like
     * catalog order, so a queue that came out sorted would fail here. */
    static const uint32_t expected[3] = {5u, 2u, 0u};
    for (uint32_t position = 0u; position < 3u; ++position) {
        uint32_t id = 0xFFFFFFFFu;
        check(helikon_queue_current(&queue, &id) == HELIKON_OK,
              "the queue has a current track");
        check(id == expected[position], "the queue follows the playlist order");
        int moved = helikon_queue_next(&queue);
        check(position + 1u < 3u ? moved == HELIKON_QUEUE_MOVED
                                : moved == HELIKON_QUEUE_END,
              "next walks the queue and then stops");
    }

    /* Rewind and play the first queued track from the volume. The path comes
     * from the catalog, so this covers queue id -> track record -> path -> file
     * -> decoder, which is the whole chain the Music hub will drive. */
    check(helikon_queue_select(&queue, 0u) == HELIKON_OK, "select the first id");
    uint32_t id = 0xFFFFFFFFu;
    check(helikon_queue_current(&queue, &id) == HELIKON_OK && id == 5u,
          "the first queued track is the one the playlist named");

    struct hdb_track queued;
    char queued_path[HELIKON_LIBRARY_PATH_MAX];
    uint32_t length = 0u;
    check(hdb_track(db, id, &queued) == HELIKON_OK, "the queued track reads");
    check(hdb_string(db, queued.path_off, queued_path, sizeof(queued_path),
                     &length) == HELIKON_OK,
          "the queued track path reads");
    check(strcmp(queued_path,
                 "/Music/Various Artists/Mixtape/02 Tide.m4a") == 0,
          "the queued track is the one the playlist named by path");

    uint64_t energy = play_catalog_path(vfs, queued_path);
    check(energy != 0u, "the queued track plays from the volume");

    /* The other resolved entries are real files too, so the queue is not one
     * playable track and two names that merely matched the catalog. */
    check(play_catalog_path(vfs, "/Music/Lighthouse/Static Bloom/01 Signal.m4a") !=
              0u,
          "the second queued track plays from the volume");
    check(play_catalog_path(vfs,
                            "/Music/The Ember Set/Night Harbour/01 First "
                            "Light.m4a") != 0u,
          "the third queued track plays from the volume");

    /* The counts are the evidence, so they are printed either way; the word in
     * front is only PASS when every check above held. */
    printf("%s playlist: %u entries -> %u queued, %u missing, %u refused\n",
           failures == 0 ? "PASS" : "FAIL", result.entries, result.added,
           result.missing, result.refused);
}

/* What a track sounds like from the volume: pull frames, sum magnitudes. */
static uint64_t sound_energy(uint32_t frames)
{
    if (captured_source == NULL)
        return 0u;
    static int16_t pcm[8192];
    size_t got = pull_frames(captured_source, captured_context, pcm, frames);
    uint64_t energy = 0u;
    for (size_t index = 0u; index < got * 2u; ++index)
        energy += (uint64_t)(pcm[index] < 0 ? -(int32_t)pcm[index]
                                            : (int32_t)pcm[index]);
    return energy;
}

/*
 * The storage-backed music source: one object owning mount + catalog + index +
 * queue + player. This is the combination nothing owned before, and the test
 * walks it the way the Music hub will: open a volume, play a playlist that is a
 * file on it, then step through the queue.
 */
static void test_music_source(struct vfs *vfs, const struct hdb *db)
{
    static struct helikon_music_source music;
    struct helikon_pmu pmu;
    struct helikon_clock clock;
    memset(&pmu, 0, sizeof(pmu));
    memset(&clock, 0, sizeof(clock));

    check(helikon_music_source_open(&music, vfs, db, &pmu, &clock, 2000u) ==
              HELIKON_OK,
          "the music source opens on the volume");
    check(music.index.used == 6u, "it indexed the catalog once");
    check(music.index.complete == 1u, "the whole fixture catalog is indexed");
    check(music.playing == 0u, "opening does not start a track");

    struct helikon_playlist_queue_result result;
    check(helikon_music_source_play_playlist(&music, "/Playlists/first.m3u",
                                            &result) == HELIKON_OK,
          "the playlist on the volume plays");
    check(result.added == 3u && result.missing == 1u && result.refused == 1u,
          "the importer's counts survive the source");

    uint32_t id = 0xFFFFFFFFu;
    check(helikon_music_source_current(&music, &id) == HELIKON_OK && id == 5u,
          "the queue is where the playlist pointed");

    char path[HELIKON_LIBRARY_PATH_MAX];
    check(helikon_music_source_path(&music, path, sizeof(path)) == HELIKON_OK &&
              strcmp(path, "/Music/Various Artists/Mixtape/02 Tide.m4a") == 0,
          "the playing path is the catalog's own path");
    check(music.playing == 1u, "the first entry is playing");
    check(captured_source != NULL, "the source started a stream");
    check(sound_energy(2048u) != 0u, "and it is not silence");

    /* next: the queue moves and the file behind it is reopened. */
    check(helikon_music_source_next(&music) == HELIKON_OK,
          "next plays the second queued track");
    check(music.track == 2u, "which is track 2");
    check(helikon_music_source_path(&music, path, sizeof(path)) == HELIKON_OK &&
              strcmp(path, "/Music/Lighthouse/Static Bloom/01 Signal.m4a") == 0,
          "the path changed with the track");
    check(music.player.opened != 0, "the player opened the new file");
    check(sound_energy(2048u) != 0u, "the second track is not silence");

    check(helikon_music_source_next(&music) == HELIKON_OK,
          "next plays the third queued track");
    check(music.track == 0u, "which is track 0");

    /* The end of the queue stops playback and says so; it does not wrap. */
    check(helikon_music_source_next(&music) == HELIKON_ERR_NOT_FOUND,
          "the end of the queue is NOT_FOUND");
    check(music.playing == 0u, "and playback stopped");
    check(helikon_music_source_current(&music, &id) == HELIKON_OK && id == 0u,
          "the queue position stayed on the last track");

    /* Back one: previous walks the same order in reverse. */
    check(helikon_music_source_previous(&music) == HELIKON_OK,
          "previous plays the track before it");
    check(music.track == 2u, "which is track 2 again");

    /* Repeat one restarts the same id instead of moving. */
    check(helikon_queue_set_repeat(&music.queue, HELIKON_QUEUE_REPEAT_ONE) ==
              HELIKON_OK,
          "repeat one is selectable");
    check(helikon_music_source_next(&music) == HELIKON_OK && music.track == 2u,
          "repeat one replays the same track");

    /* Repeat all walks to the end and then wraps to the first entry. */
    check(helikon_queue_set_repeat(&music.queue, HELIKON_QUEUE_REPEAT_ALL) ==
              HELIKON_OK,
          "repeat all is selectable");
    check(helikon_music_source_next(&music) == HELIKON_OK && music.track == 0u,
          "repeat all moves to the last queued track");
    check(helikon_music_source_next(&music) == HELIKON_OK && music.track == 5u,
          "repeat all wraps to the first queued track");
    check(helikon_queue_set_repeat(&music.queue, HELIKON_QUEUE_REPEAT_OFF) ==
              HELIKON_OK,
          "repeat off again");

    /* A playlist whose entries are all stale resolves to nothing and stops. */
    check(helikon_music_source_play_playlist(&music, "/Playlists/stale.m3u",
                                            &result) == HELIKON_ERR_NOT_FOUND,
          "a playlist with no live entry is NOT_FOUND");
    check(result.added == 0u && result.missing == 1u,
          "and it says why nothing played");
    check(music.playing == 0u, "with playback stopped rather than lying");

    /* A folder is not a playlist, and a missing path is missing. */
    check(helikon_music_source_play_playlist(&music, "/Playlists", &result) ==
              HELIKON_ERR_NOT_FOUND,
          "a folder is not a playlist");
    check(helikon_music_source_play_playlist(&music, "/Playlists/none.m3u",
                                            &result) == HELIKON_ERR_NOT_FOUND,
          "a missing playlist is NOT_FOUND");

    /* A path that does not fit is refused, not truncated. */
    check(helikon_music_source_play_track(&music, 1u) == HELIKON_OK,
          "a track can be played directly");
    char tiny[4];
    check(helikon_music_source_path(&music, tiny, sizeof(tiny)) ==
              HELIKON_ERR_BUFFER,
          "a path that does not fit is BUFFER");

    check(helikon_music_source_play_track(&music, 6u) != HELIKON_OK,
          "a track id past the catalog is refused");
    check(music.playing == 0u, "and the failed open left nothing playing");

    helikon_music_source_close(&music);
    helikon_music_source_close(&music);
    check(music.track == HELIKON_MUSIC_SOURCE_NO_TRACK && music.playing == 0u,
          "close is idempotent");

    /* Reopening rebuilds the index and plays again. */
    check(helikon_music_source_open(&music, vfs, db, &pmu, &clock, 2000u) ==
              HELIKON_OK &&
              helikon_music_source_play_track(&music, 4u) == HELIKON_OK,
          "a reopened source plays a track");
    check(music.playing == 1u, "and it is playing");
    helikon_music_source_close(&music);

    printf("%s music source: one object over the volume, 3 queued, walked 5, 2, "
           "0 and stopped at the end\n",
           failures == 0u ? "PASS" : "FAIL");
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <hfs-image>\n", argv[0]);
        return 2;
    }

    image = fopen(argv[1], "rb");
    if (image == NULL) {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    if (fseek(image, 0, SEEK_END) != 0) {
        fprintf(stderr, "cannot size %s\n", argv[1]);
        return 2;
    }
    image_size = ftell(image);
    if (image_size <= 0) {
        fprintf(stderr, "empty image %s\n", argv[1]);
        return 2;
    }

    /* macOS uses 4 KiB or 8 KiB catalog nodes; the scratch holds one node. */
    static uint8_t catalog_scratch[8192];
    static uint8_t extents_scratch[8192];

    struct vfs vfs;
    int rc = vfs_mount(&vfs, image_read, NULL, 0u, (uint64_t)image_size,
                       catalog_scratch, sizeof(catalog_scratch),
                       extents_scratch, sizeof(extents_scratch));
    check(rc == HELIKON_OK, "mount the HFS+ image read-only");
    if (rc != HELIKON_OK) {
        fprintf(stderr, "mount failed: %d\n", rc);
        struct hfs_volume volume;
        int probe = hfs_volume_probe_readonly(&volume, image_read, NULL, 0u,
                                              (uint64_t)image_size);
        fprintf(stderr,
                "probe=%d block=%u blocks=%u journaled=%u dirty=%u "
                "catalog=%u blocks size=%llu\n",
                probe, volume.block_size, volume.blocks, volume.journaled,
                volume.journal_dirty, volume.catalog.blocks,
                (unsigned long long)volume.catalog.size);
        fprintf(stderr,
                "probe=%d block=%u blocks=%u journaled=%u dirty=%u "
                "catalog=%u blocks extents=%u blocks\n",
                probe, volume.block_size, volume.blocks, volume.journaled,
                volume.journal_dirty, volume.catalog.blocks,
                volume.extents.blocks);
        return 1;
    }
    check(vfs.journal_dirty == 0u, "journal is clean after a proper unmount");

    struct vfs_entry entry;
    check(vfs_stat(&vfs, "/.helikon/music.hdb", &entry) == HELIKON_OK,
          "stat /.helikon/music.hdb");
    check(entry.kind == VFS_KIND_FILE, "the catalog is a file");
    check(entry.size > 128u, "the catalog is larger than its header");

    struct hdb_vfs_io io;
    check(hdb_vfs_open(&io, &vfs, "/.helikon/music.hdb") == HELIKON_OK,
          "open the catalog through the VFS");
    check(io.size == (uint32_t)entry.size, "adapter reports the file size");

    struct hdb_io hdb_io_vfs;
    hdb_vfs_io_init(&io, &hdb_io_vfs);

    struct hdb db;
    check(hdb_open(&db, &hdb_io_vfs) == HELIKON_OK, "hdb_open over the VFS");
    check(db.track_count == 6u, "six fixture tracks");
    check(db.album_count == 3u, "three fixture albums");
    check(db.artist_count == 3u, "three fixture artists");

    struct hdb_track track;
    char text[128];
    uint32_t length = 0u;
    check(hdb_track(&db, 0u, &track) == HELIKON_OK, "track 0 reads");
    check(hdb_string(&db, track.title_off, text, sizeof(text), &length) ==
              HELIKON_OK,
          "track 0 title reads");
    check(strcmp(text, "First Light") == 0, "track 0 title");

    struct hdb_album album;
    check(hdb_album(&db, 0u, &album) == HELIKON_OK, "album 0 reads");
    check(hdb_string(&db, album.title_off, text, sizeof(text), &length) ==
              HELIKON_OK,
          "album 0 title reads");
    check(strcmp(text, "Night Harbour") == 0, "album 0 title");

    uint32_t count = 0u;
    uint32_t index_offset = 0u;
    check(hdb_index(&db, HDB_INDEX_TITLE, &count, &index_offset) == HELIKON_OK,
          "title index reads");
    check(count == 6u, "title index count");

    /* CRC over the whole catalog read through the VFS: every byte of the file
     * has to arrive correctly, not just the record we happened to look at. */
    check(hdb_verify_crc(&db) == HELIKON_OK, "catalog CRC over the VFS");

    /* A playlist on the volume, resolved through the index, played from disk.
     * Runs before the adapter is closed: the catalog reads through it. */
    test_playlist_queue(&vfs, &db);

    /* The same path through the object the hub will own, including next and
     * previous over the queue. */
    test_music_source(&vfs, &db);

    struct hdb_vfs_io missing;
    check(hdb_vfs_open(&missing, &vfs, "/.helikon/missing.hdb") ==
              HELIKON_ERR_NOT_FOUND,
          "a missing catalog is NOT_FOUND");
    struct hdb_vfs_io folder;
    check(hdb_vfs_open(&folder, &vfs, "/.helikon") == HELIKON_ERR_IS_DIR,
          "a folder is not a catalog");

    hdb_vfs_close(&io);
    hdb_vfs_close(&io);
    check(io.open == 0, "close is idempotent");
    const struct hdb_io *closed = &hdb_io_vfs;
    check(closed->read(&io, 0u, text, 4u) == HELIKON_ERR_ARGUMENT,
          "a closed adapter refuses reads");

    /* Audio that exists only on this volume: VFS -> window -> codec -> PCM. */
    test_storage_playback(&vfs);

    if (fclose(image) != 0)
        return 2;
    if (failures != 0) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("PASS library: HFS+ -> VFS -> HDB (%u tracks, %u albums, CRC OK) "
           "and audio decodes from the volume\n",
           db.track_count, db.album_count);
    return 0;
}
