/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "music.h"

#include <stddef.h>

#include "error.h"

#ifdef HELIKON_HDB_EMBEDDED

#include "hdb.h"
#include "launcher_assets.h"
#include "mem.h"
#include "ui_text.h"

#ifdef HELIKON_AUDIO_EMBEDDED
#include "player.h"
extern const uint8_t helikon_audio_start[];
extern const uint8_t helikon_audio_end[];
static struct helikon_player player;
#endif

#define MUSIC_PAPER 0xef7cu
#define MUSIC_PAPER_LIGHT 0xf79du
#define MUSIC_INK 0x2103u
#define MUSIC_MUTED 0x6b2bu
#define MUSIC_RULE 0xb573u
#define MUSIC_FAINT 0xdeb9u
#define MUSIC_ERROR 0xf986u

#define MUSIC_X 16u
#define MUSIC_RIGHT 304u
#define MUSIC_COLUMN 288u

#define MUSIC_FONT_11  HELIKON_FONT_11_400
#define MUSIC_FONT_11M HELIKON_FONT_11_500
#define MUSIC_FONT_12  HELIKON_FONT_12_400
#define MUSIC_FONT_14  HELIKON_FONT_14_400
#define MUSIC_FONT_14M HELIKON_FONT_14_500
#define MUSIC_FONT_17  HELIKON_FONT_17_600

/* Host-preview-only flags; the firmware passes 0. */
#define MUSIC_PREVIEW_LONG 0x0001u
#define MUSIC_PREVIEW_NO_ART 0x0002u
#define MUSIC_PREVIEW_EMPTY 0x0004u
#define MUSIC_PREVIEW_CORRUPT 0x0008u
#define MUSIC_PREVIEW_CREDITS 0x0010u
#define MUSIC_PREVIEW_ALL_PLAYABLE 0x0020u

/*
 * 0 = 12 bars at 10 Hz, 1 = four bars at 5 Hz, 2 = baseline only.
 * The firmware default is full; reduced/off remain available as fallbacks.
 */
#ifndef HELIKON_MUSIC_VISUALIZER_MODE
#define HELIKON_MUSIC_VISUALIZER_MODE 0u
#endif
#define MUSIC_ART_SIZE 96u
#define MUSIC_ROWS 8u
#define MUSIC_ROW_TOP 44u
#define MUSIC_ROW_HEIGHT 21u
#define MUSIC_IDLE_MS 60000u

/* Album-wall geometry from assets/ui/album-wall/README.md. */
#define MUSIC_GRID_COLUMNS 4u
#define MUSIC_GRID_ROWS 3u
#define MUSIC_GRID_PAGE (MUSIC_GRID_COLUMNS * MUSIC_GRID_ROWS)
#define MUSIC_GRID_X 29u
#define MUSIC_GRID_Y 44u
#define MUSIC_CELL_SIZE 52u
#define MUSIC_TILE_SIZE 46u
#define MUSIC_CELL_STEP_X 70u
#define MUSIC_CELL_STEP_Y 60u
#define MUSIC_CAPTION_Y 221u

/* Artist detail page, from assets/ui/artist-view. */
#define ARTIST_BODY_X 16u
#define ARTIST_BODY_Y 176u
#define ARTIST_ALBUM_CELLS 4u
#define ARTIST_TRACK_ROWS 3u

/* Generated header order in launcher_assets.c. */
#define MUSIC_HEADER_ALBUMS 0u
#define MUSIC_HEADER_ARTISTS 1u
#define MUSIC_HEADER_TRACKS 2u
#define MUSIC_HEADER_ALBUM 3u
#define MUSIC_HEADER_ARTIST 4u
#define MUSIC_HEADER_TRACK 5u

extern const uint8_t helikon_hdb_start[];
extern const uint8_t helikon_hdb_end[];

static struct hdb db;
static uint16_t art_pixels[MUSIC_ART_SIZE * MUSIC_ART_SIZE];
static char text_buffer[192];
static struct helikon_screen *screen;
static struct helikon_buttons *buttons;
static struct helikon_clock *clock;

#ifdef HELIKON_AUDIO_EMBEDDED
static uint8_t visualizer_mode = (uint8_t)HELIKON_MUSIC_VISUALIZER_MODE;
#endif

struct nav_state {
    uint32_t previous_events;
    int32_t wheel_accumulator;
    uint64_t last_wheel_us;
    uint64_t last_move_us;
};

enum list_kind {
    LIST_ARTISTS,
    LIST_ALBUMS,
    LIST_SONGS,
    LIST_ARTIST_ALBUMS,
    LIST_ARTIST_TRACKS
};

struct list_spec {
    enum list_kind kind;
    uint32_t parent;
};

enum wall_kind {
    WALL_ALBUMS,
    WALL_ARTISTS
};

struct wall_item {
    uint32_t id;
    uint32_t artwork_id;
    char title[96];
    char subtitle[96];
};

static void copy_text(char *dest, uint32_t capacity, const char *source)
{
    if (dest == NULL || capacity == 0u)
        return;
    uint32_t index = 0u;
    if (source != NULL) {
        while (source[index] != '\0' && index + 1u < capacity) {
            dest[index] = source[index];
            ++index;
        }
    }
    dest[index] = '\0';
}

static void append_text(char *dest, uint32_t capacity, const char *source)
{
    if (dest == NULL || capacity == 0u)
        return;
    uint32_t length = 0u;
    while (length + 1u < capacity && dest[length] != '\0')
        ++length;
    copy_text(dest + length, capacity - length, source);
}

static void append_u32(char *dest, uint32_t capacity, uint32_t value)
{
    char digits[11];
    uint32_t count = 0u;
    if (value == 0u) {
        copy_text(digits, sizeof(digits), "0");
        count = 1u;
    } else {
        while (value != 0u && count < sizeof(digits) - 1u) {
            digits[count++] = (char)('0' + (value % 10u));
            value /= 10u;
        }
        for (uint32_t i = 0u; i < count / 2u; ++i) {
            char swap = digits[i];
            digits[i] = digits[count - 1u - i];
            digits[count - 1u - i] = swap;
        }
        digits[count] = '\0';
    }
    append_text(dest, capacity, digits);
}

static char latin1_ascii(uint8_t value)
{
    if (value <= 0x85u)
        return 'A';
    if (value <= 0x87u)
        return 'C';
    if (value <= 0x8bu)
        return 'E';
    if (value <= 0x8fu)
        return 'I';
    if (value == 0x90u)
        return 'D';
    if (value == 0x91u)
        return 'N';
    if (value <= 0x96u)
        return 'O';
    if (value <= 0x99u)
        return 'U';
    if (value == 0x9au)
        return 'U';
    if (value == 0x9bu || value == 0x9cu)
        return 's';
    if (value == 0x9du)
        return 'Y';
    if (value <= 0x9fu)
        return 'y';
    if (value <= 0xa5u)
        return 'a';
    if (value <= 0xa7u)
        return 'c';
    if (value <= 0xabu)
        return 'e';
    if (value <= 0xafu)
        return 'i';
    if (value == 0xb0u)
        return 'd';
    if (value == 0xb1u)
        return 'n';
    if (value <= 0xb6u)
        return 'o';
    if (value <= 0xb9u)
        return 'u';
    if (value == 0xbau)
        return 'u';
    if (value == 0xbbu || value == 0xbcu)
        return 's';
    if (value == 0xbdu)
        return 'y';
    if (value <= 0xbfu)
        return 'y';
    return ' ';
}

/* The 3x5 fallback font is ASCII; transliterate common UTF-8 names instead
 * of dropping letters. The first-party Plex atlas replaces this. */
static void ascii_text(char *dest, uint32_t capacity, const char *source,
                       uint32_t max_chars)
{
    if (dest == NULL || capacity == 0u)
        return;
    uint32_t out = 0u;
    size_t index = 0u;
    while (source != NULL && source[index] != '\0' && out < max_chars &&
           out + 1u < capacity) {
        uint8_t byte = (uint8_t)source[index];
        char value = ' ';
        if (byte < 0x80u) {
            value = (char)byte;
            index += 1u;
        } else if (byte == 0xc3u && source[index + 1u] != '\0') {
            value = latin1_ascii((uint8_t)source[index + 1u]);
            index += 2u;
        } else if (byte == 0xc2u && source[index + 1u] != '\0') {
            value = ' ';
            index += 2u;
        } else if (byte == 0xe2u && source[index + 1u] == (char)0x80u &&
                   source[index + 2u] != '\0') {
            uint8_t next = (uint8_t)source[index + 2u];
            if (next == 0x99u || next == 0x98u)
                value = '\'';
            else if (next == 0x93u || next == 0x94u)
                value = '-';
            else if (next == 0xa6u)
                value = '.';
            index += 3u;
        } else {
            index += 1u;
        }
        dest[out++] = value;
    }
    dest[out] = '\0';
}

static void draw_text(struct helikon_screen *target, uint32_t x, uint32_t y,
                      uint32_t scale, const char *source, uint32_t max_chars)
{
    ascii_text(text_buffer, sizeof(text_buffer), source, max_chars);
    helikon_screen_set_foreground(target, target->foreground);
    helikon_screen_text(target, x, y, scale, text_buffer);
}


static uint32_t text_width(const char *text, uint32_t scale)
{
    uint32_t length = 0u;
    if (text == NULL)
        return 0u;
    while (text[length] != '\0')
        ++length;
    return length == 0u ? 0u : length * 4u * scale - scale;
}

static void draw_text_right(struct helikon_screen *target, uint32_t right,
                            uint32_t y, uint32_t scale, const char *source,
                            uint32_t max_chars)
{
    ascii_text(text_buffer, sizeof(text_buffer), source, max_chars);
    uint32_t width = text_width(text_buffer, scale);
    uint32_t x = right > width ? right - width : 0u;
    helikon_screen_text(target, x, y, scale, text_buffer);
}

static void draw_diamond(struct helikon_screen *target, uint32_t cx,
                         uint32_t cy, int radius, uint16_t color)
{
    for (int dy = -radius; dy <= radius; ++dy) {
        int distance = dy < 0 ? -dy : dy;
        int half = radius - distance;
        uint32_t x = (uint32_t)((int32_t)cx - half);
        uint32_t y = (uint32_t)((int32_t)cy + dy);
        helikon_screen_fill_rect(target, x, y, (uint32_t)(half * 2 + 1), 1u,
                                 color);
    }
}

static int blob_read(void *context, uint32_t offset, void *buffer,
                     uint32_t length)
{
    (void)context;
    size_t size = (size_t)(helikon_hdb_end - helikon_hdb_start);
    if ((size_t)offset > size || (size_t)length > size - (size_t)offset)
        return HELIKON_ERR_IO;
    helikon_memcpy(buffer, helikon_hdb_start + offset, length);
    return HELIKON_OK;
}

static int read_string(uint32_t offset, char *buffer, uint32_t capacity)
{
    if (buffer == NULL || capacity == 0u)
        return HELIKON_ERR_ARGUMENT;
    int rc = hdb_string(&db, offset, buffer, capacity, NULL);
    if (rc == HELIKON_OK)
        return HELIKON_OK;
    buffer[0] = '\0';
    if (rc != HELIKON_ERR_BUFFER || capacity < 5u || offset == HDB_NO_ID ||
        offset == 0u)
        return HELIKON_ERR_IO;

    /*
     * HDB strings are length-prefixed. When the caller's buffer is shorter
     * than the value, copy a UTF-8-safe prefix and append a visible ellipsis
     * instead of silently dropping the title. The renderer may ellipsize
     * again at the target column; the result is still one visible U+2026.
     */
    uint8_t header[2];
    if (db.io.read(db.io.context, offset, header, sizeof(header)) != 0)
        return HELIKON_ERR_IO;
    uint32_t total = (uint32_t)header[0] |
                     ((uint32_t)header[1] << 8);
    uint32_t copy = capacity - 4u;
    if (copy > total)
        copy = total;
    if (copy != 0u &&
        db.io.read(db.io.context, offset + 2u, buffer, copy) != 0) {
        buffer[0] = '\0';
        return HELIKON_ERR_IO;
    }
    if (copy < total) {
        uint8_t next = 0u;
        if (db.io.read(db.io.context, offset + 2u + copy, &next, 1u) != 0) {
            buffer[0] = '\0';
            return HELIKON_ERR_IO;
        }
        if ((next & 0xc0u) == 0x80u) {
            while (copy > 0u &&
                   ((uint8_t)buffer[copy - 1u] & 0xc0u) == 0x80u)
                --copy;
            if (copy > 0u)
                --copy;
        }
    }
    buffer[copy] = '\0';
    append_text(buffer, capacity, "\xe2\x80\xa6");
    return HELIKON_OK;
}

static void read_str(uint32_t offset, char *buffer, uint32_t capacity)
{
    (void)read_string(offset, buffer, capacity);
}

static int load_art(uint32_t artwork_id)
{
    if (artwork_id == HDB_NO_ID)
        return 0;
    struct hdb_art art;
    if (hdb_art(&db, artwork_id, &art) != HELIKON_OK)
        return 0;
    if (art.width != MUSIC_ART_SIZE || art.height != MUSIC_ART_SIZE)
        return 0;
    uint32_t length = 0u;
    if (hdb_art_read(&db, artwork_id, art_pixels, sizeof(art_pixels),
                     &length) != HELIKON_OK)
        return 0;
    return length == sizeof(art_pixels);
}

static void draw_scaled_art(struct helikon_screen *target, uint32_t x,
                            uint32_t y, uint32_t size, uint32_t source_size)
{
    if (target == NULL || target->pixels == NULL || size == 0u ||
        source_size == 0u)
        return;
    for (uint32_t row = 0u; row < size; ++row) {
        if (y + row >= target->height)
            break;
        uint32_t source_row = row * source_size / size;
        uint16_t *line =
            target->pixels + (size_t)(y + row) * target->width + x;
        for (uint32_t column = 0u; column < size; ++column) {
            if (x + column >= target->width)
                break;
            uint32_t source_column = column * source_size / size;
            line[column] =
                art_pixels[source_row * source_size + source_column];
        }
    }
}

static void draw_monogram(struct helikon_screen *target, uint32_t x,
                          uint32_t y, const char *name)
{
    helikon_screen_fill_rect(target, x, y, MUSIC_TILE_SIZE, MUSIC_TILE_SIZE,
                             MUSIC_PAPER_LIGHT);
    char text[96];
    ascii_text(text, sizeof(text), name, 1u);
    char initial[2];
    initial[0] = text[0] != '\0' ? text[0] : ' ';
    initial[1] = '\0';
    uint32_t scale = 4u;
    uint32_t width = text_width(initial, scale);
    uint32_t height = 5u * scale;
    uint32_t tx = x + (MUSIC_TILE_SIZE - width) / 2u;
    uint32_t ty = y + (MUSIC_TILE_SIZE - height) / 2u;
    helikon_screen_set_foreground(target, MUSIC_MUTED);
    helikon_screen_text(target, tx, ty, scale, initial);
    helikon_screen_set_foreground(target, MUSIC_INK);
}

static void draw_wall_cell(struct helikon_screen *target, uint32_t x,
                           uint32_t y, int selected,
                           const struct wall_item *item)
{
    helikon_screen_fill_rect(target, x, y, MUSIC_CELL_SIZE, MUSIC_CELL_SIZE,
                             selected ? MUSIC_INK : MUSIC_RULE);
    helikon_screen_fill_rect(target, x + 1u, y + 1u, MUSIC_CELL_SIZE - 2u,
                             MUSIC_CELL_SIZE - 2u, MUSIC_PAPER);
    if (selected) {
        helikon_screen_fill_rect(target, x, y, MUSIC_CELL_SIZE, 2u, MUSIC_INK);
        helikon_screen_fill_rect(target, x, y + MUSIC_CELL_SIZE - 2u,
                                 MUSIC_CELL_SIZE, 2u, MUSIC_INK);
        helikon_screen_fill_rect(target, x, y, 2u, MUSIC_CELL_SIZE, MUSIC_INK);
        helikon_screen_fill_rect(target, x + MUSIC_CELL_SIZE - 2u, y, 2u,
                                 MUSIC_CELL_SIZE, MUSIC_INK);
    }

    uint32_t art_x = x + 3u;
    uint32_t art_y = y + 3u;
    if (item->artwork_id != HDB_NO_ID && load_art(item->artwork_id) != 0)
        draw_scaled_art(target, art_x, art_y, MUSIC_TILE_SIZE, MUSIC_ART_SIZE);
    else
        draw_monogram(target, art_x, art_y, item->title);
}

static uint32_t wall_count(enum wall_kind kind)
{
    if (kind == WALL_ARTISTS)
        return db.artist_count;
    uint32_t count = 0u;
    uint32_t offset = 0u;
    if (hdb_index(&db, HDB_INDEX_ALBUM_TITLE, &count, &offset) != HELIKON_OK)
        return 0u;
    return count;
}

static int wall_item(enum wall_kind kind, uint32_t index,
                     struct wall_item *item)
{
    if (item == NULL)
        return HELIKON_ERR_ARGUMENT;
    item->title[0] = '\0';
    item->subtitle[0] = '\0';
    item->artwork_id = HDB_NO_ID;

    if (kind == WALL_ALBUMS) {
        uint32_t album_id = 0u;
        if (hdb_index_get(&db, HDB_INDEX_ALBUM_TITLE, index, &album_id) !=
            HELIKON_OK)
            return HELIKON_ERR_NOT_FOUND;
        struct hdb_album album;
        if (hdb_album(&db, album_id, &album) != HELIKON_OK)
            return HELIKON_ERR_NOT_FOUND;
        read_str(album.title_off, item->title, sizeof(item->title));
        item->artwork_id = album.artwork_id;
        struct hdb_artist artist;
        if (hdb_artist(&db, album.artist_id, &artist) == HELIKON_OK)
            read_str(artist.name_off, item->subtitle,
                     sizeof(item->subtitle));
        item->id = album_id;
        return HELIKON_OK;
    }

    struct hdb_artist artist;
    if (hdb_artist(&db, index, &artist) != HELIKON_OK)
        return HELIKON_ERR_NOT_FOUND;
    read_str(artist.name_off, item->title, sizeof(item->title));
    item->artwork_id = artist.image_id;
    append_u32(item->subtitle, sizeof(item->subtitle), artist.album_count);
    append_text(item->subtitle, sizeof(item->subtitle),
                artist.album_count == 1u ? " ALBUM" : " ALBUMS");
    item->id = index;
    return HELIKON_OK;
}

static uint32_t artist_track_count(uint32_t artist_id)
{
    struct hdb_artist artist;
    if (hdb_artist(&db, artist_id, &artist) != HELIKON_OK)
        return 0u;
    return artist.track_count;
}

static void artist_genre(uint32_t artist_id, char *buffer, uint32_t capacity)
{
    if (buffer == NULL || capacity == 0u)
        return;
    buffer[0] = '\0';
    struct hdb_artist artist;
    if (hdb_artist(&db, artist_id, &artist) != HELIKON_OK)
        return;
    for (uint32_t index = 0u; index < artist.album_count; ++index) {
        struct hdb_album album;
        if (hdb_album(&db, artist.first_album + index, &album) != HELIKON_OK)
            continue;
        read_str(album.genre_off, buffer, capacity);
        if (buffer[0] != '\0')
            return;
    }
}


static int artist_album_at(uint32_t artist_id, uint32_t index,
                           uint32_t *album_id)
{
    if (album_id == NULL)
        return HELIKON_ERR_ARGUMENT;
    struct hdb_artist artist;
    if (hdb_artist(&db, artist_id, &artist) != HELIKON_OK ||
        index >= artist.album_count)
        return HELIKON_ERR_NOT_FOUND;
    uint32_t value = artist.first_album + index;
    struct hdb_album album;
    if (hdb_album(&db, value, &album) != HELIKON_OK ||
        album.artist_id != artist_id)
        return HELIKON_ERR_NOT_FOUND;
    *album_id = value;
    return HELIKON_OK;
}

static int artist_track_at(uint32_t artist_id, uint32_t index,
                           uint32_t *track_id)
{
    if (track_id == NULL)
        return HELIKON_ERR_ARGUMENT;

    struct hdb_artist artist;
    if (hdb_artist(&db, artist_id, &artist) != HELIKON_OK)
        return HELIKON_ERR_NOT_FOUND;

    /*
     * HDB sorts albums by artist and tracks by album, so one artist's tracks
     * are contiguous. Use that path first; the scan is a safety fallback for
     * an externally built catalog with a different order.
     */
    uint32_t total = 0u;
    uint32_t candidate = HDB_NO_ID;
    for (uint32_t album_index = 0u; album_index < artist.album_count;
         ++album_index) {
        struct hdb_album album;
        if (hdb_album(&db, artist.first_album + album_index, &album) !=
            HELIKON_OK)
            continue;
        if (album.artist_id != artist_id)
            continue;
        if (index >= total && index < total + album.track_count) {
            candidate = album.first_track + (index - total);
            break;
        }
        total += album.track_count;
    }
    if (candidate != HDB_NO_ID) {
        struct hdb_track track;
        if (hdb_track(&db, candidate, &track) == HELIKON_OK &&
            track.artist_id == artist_id) {
            *track_id = candidate;
            return HELIKON_OK;
        }
    }

    uint32_t seen = 0u;
    for (uint32_t track = 0u; track < db.track_count; ++track) {
        struct hdb_track item;
        if (hdb_track(&db, track, &item) != HELIKON_OK)
            continue;
        if (item.artist_id != artist_id)
            continue;
        if (seen == index) {
            *track_id = track;
            return HELIKON_OK;
        }
        ++seen;
    }
    return HELIKON_ERR_NOT_FOUND;
}


static void blit_header(struct helikon_screen *target, uint32_t header)
{
    const struct helikon_launcher_bitmap *strip =
        &helikon_music_headers[header];
    helikon_screen_bitmap(target, (uint32_t)strip->x, (uint32_t)strip->y,
                          strip->width, strip->height, strip->pixels,
                          strip->width);
}


static void draw_wall_header(struct helikon_screen *target, uint32_t header,
                             uint32_t count, uint32_t count_right,
                             uint32_t count_y)
{
    blit_header(target, header);
    char text[16];
    copy_text(text, sizeof(text), "");
    append_u32(text, sizeof(text), count);
    helikon_screen_set_foreground(target, MUSIC_MUTED);
    draw_text_right(target, count_right, count_y, 2u, text, 8u);
    helikon_screen_set_foreground(target, MUSIC_INK);
}

static void draw_wall(struct helikon_screen *target, enum wall_kind kind,
                      uint32_t selected)
{
    uint32_t count = wall_count(kind);
    helikon_screen_fill_rect(target, 0u, 0u, target->width, target->height,
                             MUSIC_PAPER);
    uint32_t header = kind == WALL_ALBUMS ? MUSIC_HEADER_ALBUMS
                                          : MUSIC_HEADER_ARTISTS;
    draw_wall_header(target, header, count, 270u, 17u);

    if (count == 0u) {
        helikon_screen_set_foreground(target, MUSIC_INK);
        helikon_screen_text(target, MUSIC_GRID_X, 80u, 2u,
                            kind == WALL_ALBUMS ? "NO ALBUMS" : "NO ARTISTS");
        helikon_screen_set_foreground(target, MUSIC_MUTED);
        helikon_screen_text(target, 14u, 222u, 1u, "MENU: BACK");
        helikon_screen_set_foreground(target, MUSIC_INK);
        (void)helikon_screen_present_all(target);
        return;
    }

    uint32_t page = selected / MUSIC_GRID_PAGE;
    for (uint32_t slot = 0u; slot < MUSIC_GRID_PAGE; ++slot) {
        uint32_t index = page * MUSIC_GRID_PAGE + slot;
        if (index >= count)
            break;
        struct wall_item item;
        if (wall_item(kind, index, &item) != HELIKON_OK)
            continue;
        uint32_t column = slot % MUSIC_GRID_COLUMNS;
        uint32_t row = slot / MUSIC_GRID_COLUMNS;
        uint32_t x = MUSIC_GRID_X + column * MUSIC_CELL_STEP_X;
        uint32_t y = MUSIC_GRID_Y + row * MUSIC_CELL_STEP_Y;
        draw_wall_cell(target, x, y, index == selected, &item);
    }

    struct wall_item current;
    if (selected < count &&
        wall_item(kind, selected, &current) == HELIKON_OK) {
        char caption[160];
        copy_text(caption, sizeof(caption), current.title);
        if (current.subtitle[0] != '\0') {
            append_text(caption, sizeof(caption), " - ");
            append_text(caption, sizeof(caption), current.subtitle);
        }
        helikon_screen_set_foreground(target, MUSIC_INK);
        draw_text(target, MUSIC_GRID_X, MUSIC_CAPTION_Y, 2u, caption, 27u);
    }

    char position[24];
    copy_text(position, sizeof(position), "");
    append_u32(position, sizeof(position), selected + 1u);
    append_text(position, sizeof(position), " / ");
    append_u32(position, sizeof(position), count);
    helikon_screen_set_foreground(target, MUSIC_MUTED);
    draw_text_right(target, 291u, 224u, 1u, position, 10u);
    helikon_screen_set_foreground(target, MUSIC_INK);
    (void)helikon_screen_present_all(target);
}

static void draw_hub(struct helikon_screen *target, uint32_t selected)
{
    helikon_screen_fill_rect(target, 0u, 0u, target->width, target->height,
                             MUSIC_PAPER);
    helikon_screen_bitmap(target, (uint32_t)helikon_launcher_background.x,
                          (uint32_t)helikon_launcher_background.y,
                          helikon_launcher_background.width,
                          helikon_launcher_background.height,
                          helikon_launcher_background.pixels,
                          helikon_launcher_background.width);

    for (uint32_t index = 0u; index < HELIKON_MUSIC_HUB_CELLS; ++index) {
        uint32_t x = 24u + index * 104u;
        uint32_t y = 88u;
        const struct helikon_launcher_bitmap *icon =
            &helikon_music_hub_icons[index];
        helikon_screen_bitmap(target, (uint32_t)icon->x, (uint32_t)icon->y,
                              icon->width, icon->height, icon->pixels,
                              icon->width);
        helikon_screen_fill_rect(target, x + 20u, y + 56u, 24u, 1u,
                                 helikon_music_hub_plinths[index]);
        const struct helikon_launcher_bitmap *label =
            &helikon_music_hub_labels[index];
        helikon_screen_bitmap(target, (uint32_t)label->x, (uint32_t)label->y,
                              label->width, label->height, label->pixels,
                              label->width);
    }

    if (selected < HELIKON_MUSIC_HUB_CELLS) {
        const struct helikon_launcher_bitmap *label =
            &helikon_music_hub_labels[selected];
        uint32_t cell_x = 24u + selected * 104u;
        uint32_t cell_y = 88u;
        uint32_t center = (uint32_t)label->x + (uint32_t)label->width / 2u;
        uint32_t width = (uint32_t)label->width + 10u;
        uint32_t left = center - width / 2u;
        draw_diamond(target, cell_x + 32u, cell_y + 6u, 2, MUSIC_INK);
        helikon_screen_fill_rect(target, left, cell_y + 84u, width, 2u,
                                 MUSIC_INK);
    }

    helikon_screen_set_foreground(target, MUSIC_MUTED);
    helikon_screen_text(target, 14u, 222u, 1u, "MENU: BACK");
    draw_text_right(target, 306u, 222u, 1u, "SELECT: OPEN", 12u);
    helikon_screen_set_foreground(target, MUSIC_INK);
    (void)helikon_screen_present_all(target);
}

static void draw_frame(struct helikon_screen *target, const char *title)
{
    helikon_screen_fill_rect(target, 0u, 0u, target->width, target->height,
                             MUSIC_PAPER);
    helikon_screen_set_foreground(target, MUSIC_INK);
    draw_text(target, 14u, 10u, 2u, title, 40u);
    helikon_screen_fill_rect(target, 12u, 30u, 296u, 1u, MUSIC_MUTED);
    helikon_screen_set_foreground(target, MUSIC_MUTED);
    helikon_screen_text(target, 14u, 222u, 1u, "MENU: BACK");
    helikon_screen_set_foreground(target, MUSIC_INK);
}


static uint32_t list_count(const struct list_spec *spec)
{
    switch (spec->kind) {
    case LIST_ARTISTS:
        return db.artist_count;
    case LIST_ALBUMS:
    case LIST_SONGS: {
        uint32_t count = 0u;
        uint32_t offset = 0u;
        uint32_t type = spec->kind == LIST_ALBUMS ? HDB_INDEX_ALBUM_TITLE
                                                  : HDB_INDEX_TITLE;
        if (hdb_index(&db, type, &count, &offset) != HELIKON_OK)
            return 0u;
        return count;
    }
    case LIST_ARTIST_ALBUMS: {
        struct hdb_artist artist;
        if (hdb_artist(&db, spec->parent, &artist) != HELIKON_OK)
            return 0u;
        return artist.album_count;
    }
    case LIST_ARTIST_TRACKS:
        return artist_track_count(spec->parent);
    default:
        return 0u;
    }
}

/* Returns the record id for the list entry. */
static int list_item(const struct list_spec *spec, uint32_t index,
                     char *label, uint32_t capacity, uint32_t *id)
{
    char scratch[192];
    if (label == NULL || capacity == 0u || id == NULL)
        return HELIKON_ERR_ARGUMENT;

    switch (spec->kind) {
    case LIST_ARTISTS: {
        struct hdb_artist artist;
        if (hdb_artist(&db, index, &artist) != HELIKON_OK)
            return HELIKON_ERR_NOT_FOUND;
        read_str(artist.name_off, scratch, sizeof(scratch));
        copy_text(label, capacity, scratch);
        *id = index;
        return HELIKON_OK;
    }
    case LIST_ALBUMS:
    case LIST_SONGS: {
        uint32_t value = 0u;
        uint32_t type = spec->kind == LIST_ALBUMS ? HDB_INDEX_ALBUM_TITLE
                                                  : HDB_INDEX_TITLE;
        if (hdb_index_get(&db, type, index, &value) != HELIKON_OK)
            return HELIKON_ERR_NOT_FOUND;
        if (spec->kind == LIST_ALBUMS) {
            struct hdb_album album;
            if (hdb_album(&db, value, &album) != HELIKON_OK)
                return HELIKON_ERR_NOT_FOUND;
            read_str(album.title_off, scratch, sizeof(scratch));
            copy_text(label, capacity, scratch);
        } else {
            struct hdb_track track;
            if (hdb_track(&db, value, &track) != HELIKON_OK)
                return HELIKON_ERR_NOT_FOUND;
            read_str(track.title_off, scratch, sizeof(scratch));
            copy_text(label, capacity, scratch);
        }
        *id = value;
        return HELIKON_OK;
    }
    case LIST_ARTIST_ALBUMS: {
        struct hdb_artist artist;
        if (hdb_artist(&db, spec->parent, &artist) != HELIKON_OK ||
            index >= artist.album_count)
            return HELIKON_ERR_NOT_FOUND;
        uint32_t album_id = artist.first_album + index;
        struct hdb_album album;
        if (hdb_album(&db, album_id, &album) != HELIKON_OK)
            return HELIKON_ERR_NOT_FOUND;
        read_str(album.title_off, scratch, sizeof(scratch));
        copy_text(label, capacity, scratch);
        *id = album_id;
        return HELIKON_OK;
    }
    case LIST_ARTIST_TRACKS: {
        uint32_t track_id = 0u;
        if (artist_track_at(spec->parent, index, &track_id) != HELIKON_OK)
            return HELIKON_ERR_NOT_FOUND;
        struct hdb_track track;
        if (hdb_track(&db, track_id, &track) != HELIKON_OK)
            return HELIKON_ERR_NOT_FOUND;
        read_str(track.title_off, scratch, sizeof(scratch));
        if (track.track_no != 0u) {
            char prefix[8];
            copy_text(prefix, sizeof(prefix), "");
            append_u32(prefix, sizeof(prefix), track.track_no);
            append_text(prefix, sizeof(prefix), "  ");
            copy_text(label, capacity, prefix);
            append_text(label, capacity, scratch);
        } else {
            copy_text(label, capacity, scratch);
        }
        *id = track_id;
        return HELIKON_OK;
    }
    default:
        return HELIKON_ERR_NOT_FOUND;
    }
}

static void list_title(const struct list_spec *spec, uint32_t count,
                       char *title, uint32_t capacity)
{
    switch (spec->kind) {
    case LIST_ARTISTS:
        copy_text(title, capacity, "ARTISTS (");
        append_u32(title, capacity, count);
        append_text(title, capacity, ")");
        break;
    case LIST_ALBUMS:
        copy_text(title, capacity, "ALBUMS (");
        append_u32(title, capacity, count);
        append_text(title, capacity, ")");
        break;
    case LIST_SONGS:
        copy_text(title, capacity, "TRACKS (");
        append_u32(title, capacity, count);
        append_text(title, capacity, ")");
        break;
    case LIST_ARTIST_ALBUMS: {
        struct hdb_artist artist;
        if (hdb_artist(&db, spec->parent, &artist) == HELIKON_OK) {
            read_str(artist.name_off, title, capacity);
        } else {
            copy_text(title, capacity, "ALBUMS");
        }
        break;
    }
    case LIST_ARTIST_TRACKS:
        copy_text(title, capacity, "TRACKS (");
        append_u32(title, capacity, count);
        append_text(title, capacity, ")");
        break;
    default:
        copy_text(title, capacity, "MUSIC");
        break;
    }
}

static void draw_list(struct helikon_screen *target, const struct list_spec *spec,
                      uint32_t selected, uint32_t top, uint32_t count)
{
    if (spec->kind == LIST_SONGS) {
        helikon_screen_fill_rect(target, 0u, 0u, target->width,
                                 target->height, MUSIC_PAPER);
        draw_wall_header(target, MUSIC_HEADER_TRACKS, count, 291u, 17u);
        helikon_screen_set_foreground(target, MUSIC_MUTED);
        helikon_screen_text(target, 14u, 222u, 1u, "MENU: BACK");
        helikon_screen_set_foreground(target, MUSIC_INK);
    } else {
        char title[192];
        list_title(spec, count, title, sizeof(title));
        draw_frame(target, title);
    }

    for (uint32_t row = 0u; row < MUSIC_ROWS; ++row) {
        uint32_t index = top + row;
        if (index >= count)
            break;
        char label[96];
        uint32_t id = 0u;
        if (list_item(spec, index, label, sizeof(label), &id) != HELIKON_OK)
            continue;
        uint32_t y = MUSIC_ROW_TOP + row * MUSIC_ROW_HEIGHT;
        if (index == selected) {
            helikon_screen_fill_rect(target, 8u, y - 3u, 304u, 20u, MUSIC_INK);
            helikon_screen_set_foreground(target, MUSIC_PAPER);
        } else {
            helikon_screen_set_foreground(target, MUSIC_INK);
        }
        draw_text(target, 14u, y, 2u, label, 46u);
    }
    helikon_screen_set_foreground(target, MUSIC_MUTED);
    char position[24];
    copy_text(position, sizeof(position), "");
    append_u32(position, sizeof(position), selected + 1u);
    append_text(position, sizeof(position), "/");
    append_u32(position, sizeof(position), count);
    helikon_screen_text(target, 272u, 222u, 1u, position);
    helikon_screen_set_foreground(target, MUSIC_INK);
    (void)helikon_screen_present_all(target);
}

static void nav_reset(struct nav_state *nav)
{
    nav->previous_events = HELIKON_BUTTON_ANY;
    nav->wheel_accumulator = 0;
    nav->last_wheel_us = 0u;
    nav->last_move_us = 0u;
}

static int nav_delta(struct nav_state *nav, uint32_t pressed, int32_t wheel,
                     uint64_t now)
{
    const int32_t wheel_step = 4;
    const uint64_t wheel_repeat_us = 120000u;
    const uint64_t wheel_idle_us = 300000u;
    int delta = 0;

    if ((pressed & HELIKON_BUTTON_LEFT) != 0u) {
        delta = -1;
        nav->wheel_accumulator = 0;
    } else if ((pressed & HELIKON_BUTTON_RIGHT) != 0u) {
        delta = 1;
        nav->wheel_accumulator = 0;
    } else if (wheel != 0) {
        if (now - nav->last_wheel_us > wheel_idle_us)
            nav->wheel_accumulator = 0;
        nav->wheel_accumulator += wheel;
        if (nav->wheel_accumulator > wheel_step * 2)
            nav->wheel_accumulator = wheel_step * 2;
        else if (nav->wheel_accumulator < -wheel_step * 2)
            nav->wheel_accumulator = -wheel_step * 2;
        nav->last_wheel_us = now;
        if (now - nav->last_move_us >= wheel_repeat_us) {
            if (nav->wheel_accumulator >= wheel_step) {
                delta = 1;
                nav->wheel_accumulator -= wheel_step;
                nav->last_move_us = now;
            } else if (nav->wheel_accumulator <= -wheel_step) {
                delta = -1;
                nav->wheel_accumulator += wheel_step;
                nav->last_move_us = now;
            }
        }
    } else if (now - nav->last_wheel_us > wheel_idle_us) {
        nav->wheel_accumulator = 0;
    }
    return delta;
}

/* Returns the selected record id, or -1 on MENU/timeout/empty. */
static int run_list(const struct list_spec *spec, uint32_t *selected_io)
{
    uint32_t count = list_count(spec);
    uint32_t selected = selected_io != NULL ? *selected_io : 0u;
    if (count != 0u && selected >= count)
        selected = count - 1u;
    if (selected_io != NULL)
        *selected_io = selected;
    if (count == 0u) {
        draw_frame(screen, "EMPTY");
        helikon_screen_text(screen, 14u, 60u, 2u, "NO ITEMS");
        (void)helikon_screen_present_all(screen);
        uint64_t idle = helikon_clock_now_us(clock);
        for (;;) {
            uint32_t events = 0u;
            int32_t wheel = 0;
            int hold = 0;
            if (helikon_buttons_poll(buttons, &events, &wheel, &hold, 20u) ==
                    HELIKON_OK &&
                ((events & HELIKON_BUTTON_ANY) != 0u || hold))
                return -1;
            if (helikon_clock_expired(clock, idle, MUSIC_IDLE_MS))
                return -1;
            helikon_clock_delay_us(clock, 20000u);
        }
    }

    uint32_t top = (selected / MUSIC_ROWS) * MUSIC_ROWS;
    struct nav_state nav;
    nav_reset(&nav);
    uint64_t idle = helikon_clock_now_us(clock);
    draw_list(screen, spec, selected, top, count);

    for (;;) {
        uint32_t events = 0u;
        int32_t wheel = 0;
        int hold = 0;
        if (helikon_buttons_poll(buttons, &events, &wheel, &hold, 20u) ==
            HELIKON_OK) {
            uint64_t now = helikon_clock_now_us(clock);
            uint32_t pressed = events & ~nav.previous_events;
            nav.previous_events = events;

            if (hold)
                return -1;
            if ((pressed & HELIKON_BUTTON_MENU) != 0u)
                return -1;
            if ((pressed & (HELIKON_BUTTON_SELECT | HELIKON_BUTTON_PLAY)) !=
                0u) {
                char label[96];
                uint32_t id = 0u;
                if (list_item(spec, selected, label, sizeof(label), &id) ==
                    HELIKON_OK)
                    return (int)id;
                return -1;
            }
            int delta = nav_delta(&nav, pressed, wheel, now);
            if (delta != 0) {
                if (delta < 0 && selected > 0u)
                    --selected;
                else if (delta > 0 && selected + 1u < count)
                    ++selected;
                if (selected < top)
                    top = selected;
                else if (selected >= top + MUSIC_ROWS)
                    top = selected - MUSIC_ROWS + 1u;
                if (selected_io != NULL)
                    *selected_io = selected;
                idle = now;
                draw_list(screen, spec, selected, top, count);
            }
        }
        if (helikon_clock_expired(clock, idle, MUSIC_IDLE_MS))
            return -1;
        helikon_clock_delay_us(clock, 20000u);
    }
}


static void draw_rule(struct helikon_screen *target, uint32_t y,
                      uint16_t color)
{
    helikon_screen_fill_rect(target, MUSIC_X, y, MUSIC_COLUMN, 1u, color);
}

static void draw_battery(struct helikon_screen *target)
{
    helikon_screen_fill_rect(target, 282u, 11u, 18u, 1u, MUSIC_MUTED);
    helikon_screen_fill_rect(target, 282u, 19u, 18u, 1u, MUSIC_MUTED);
    helikon_screen_fill_rect(target, 282u, 11u, 1u, 9u, MUSIC_MUTED);
    helikon_screen_fill_rect(target, 299u, 11u, 1u, 9u, MUSIC_MUTED);
    helikon_screen_fill_rect(target, 300u, 14u, 2u, 3u, MUSIC_MUTED);
}

static void draw_top_bar(struct helikon_screen *target, const char *label)
{
    helikon_text_draw(target, (int32_t)MUSIC_X, 20, MUSIC_FONT_11,
                      MUSIC_MUTED, label, 0u);
    if (helikon_text_fallback_active()) {
        helikon_text_draw_right(target, 274, 20, MUSIC_FONT_11, MUSIC_MUTED,
                                "FONT FALLBACK", 110u);
    } else {
        helikon_text_draw_right(target, 274, 20, MUSIC_FONT_11, MUSIC_MUTED,
                                "--:--", 60u);
    }
    draw_battery(target);
    draw_rule(target, 30u, MUSIC_RULE);
}

static void draw_line(struct helikon_screen *target, int32_t x0, int32_t y0,
                      int32_t x1, int32_t y1, uint16_t color)
{
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int32_t dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int32_t sx = x0 < x1 ? 1 : -1;
    int32_t sy = y0 < y1 ? 1 : -1;
    int32_t error = dx - dy;
    for (;;) {
        if (x0 >= 0 && y0 >= 0 && (uint32_t)x0 < target->width &&
            (uint32_t)y0 < target->height)
            target->pixels[(size_t)y0 * target->width + (uint32_t)x0] = color;
        if (x0 == x1 && y0 == y1)
            break;
        int32_t twice = error * 2;
        if (twice > -dy) {
            error -= dy;
            x0 += sx;
        }
        if (twice < dx) {
            error += dx;
            y0 += sy;
        }
    }
}

static void draw_circle_outline(struct helikon_screen *target, int32_t cx,
                                int32_t cy, int32_t radius, uint16_t color)
{
    int32_t x = radius;
    int32_t y = 0;
    int32_t error = 1 - radius;
    while (x >= y) {
        draw_line(target, cx + x, cy + y, cx + y, cy + x, color);
        draw_line(target, cx + y, cy + x, cx - y, cy + x, color);
        draw_line(target, cx - y, cy + x, cx - x, cy + y, color);
        draw_line(target, cx - x, cy + y, cx - x, cy - y, color);
        draw_line(target, cx - x, cy - y, cx - y, cy - x, color);
        draw_line(target, cx - y, cy - x, cx + y, cy - x, color);
        draw_line(target, cx + y, cy - x, cx + x, cy - y, color);
        draw_line(target, cx + x, cy - y, cx + x, cy + y, color);
        ++y;
        if (error < 0) {
            error += 2 * y + 1;
        } else {
            --x;
            error += 2 * (y - x) + 1;
        }
    }
}

static void draw_diamond_filled(struct helikon_screen *target, int32_t cx,
                                int32_t cy, int32_t radius, uint16_t color)
{
    for (int32_t dy = -radius; dy <= radius; ++dy) {
        int32_t distance = dy < 0 ? -dy : dy;
        int32_t half = radius - distance;
        helikon_screen_fill_rect(target, (uint32_t)(cx - half),
                                 (uint32_t)(cy + dy), (uint32_t)(half * 2 + 1),
                                 1u, color);
    }
}

static void draw_diamond_outline(struct helikon_screen *target, int32_t cx,
                                 int32_t cy, int32_t radius, uint16_t color)
{
    draw_line(target, cx, cy - radius, cx + radius, cy, color);
    draw_line(target, cx + radius, cy, cx, cy + radius, color);
    draw_line(target, cx, cy + radius, cx - radius, cy, color);
    draw_line(target, cx - radius, cy, cx, cy - radius, color);
}

static void draw_play_triangle(struct helikon_screen *target, int32_t x,
                               int32_t y, int32_t width, int32_t height,
                               uint16_t color)
{
    if (width <= 0 || height <= 0)
        return;
    int32_t middle = (height - 1) / 2;
    for (int32_t row = 0; row < height; ++row) {
        int32_t distance = row <= middle ? row : height - 1 - row;
        int32_t span = 1 + (width - 1) * distance / (middle == 0 ? 1 : middle);
        helikon_screen_fill_rect(target, (uint32_t)x, (uint32_t)(y + row),
                                 (uint32_t)span, 1u, color);
    }
}

#ifdef HELIKON_AUDIO_EMBEDDED
static void draw_pause_icon(struct helikon_screen *target, int32_t x,
                            int32_t baseline, uint16_t color)
{
    helikon_screen_fill_rect(target, (uint32_t)x,
                             (uint32_t)(baseline - 10), 3u, 11u, color);
    helikon_screen_fill_rect(target, (uint32_t)(x + 6),
                             (uint32_t)(baseline - 10), 3u, 11u, color);
}
#endif

static void draw_plex_monogram(struct helikon_screen *target, uint32_t x,
                          uint32_t y, uint32_t size, const char *name)
{
    helikon_screen_fill_rect(target, x, y, size, size, MUSIC_PAPER_LIGHT);
    char initials[9];
    uint32_t out = 0u;
    uint32_t words = 0u;
    size_t index = 0u;
    while (name != NULL && name[index] != '\0' && words < 2u &&
           out + 1u < sizeof(initials)) {
        while (name[index] == ' ')
            ++index;
        if (name[index] == '\0')
            break;
        uint8_t first = (uint8_t)name[index];
        if (first < 0x80u) {
            char value = (char)first;
            if (value >= 'a' && value <= 'z')
                value = (char)(value - 'a' + 'A');
            initials[out++] = value;
            ++index;
        } else {
            size_t bytes = 2u;
            if ((first & 0xf0u) == 0xe0u)
                bytes = 3u;
            else if ((first & 0xf8u) == 0xf0u)
                bytes = 4u;
            for (size_t count = 0u; count < bytes && name[index] != '\0';
                 ++count)
                initials[out++] = name[index++];
        }
        ++words;
        while (name[index] != '\0' && name[index] != ' ')
            ++index;
    }
    if (out == 0u) {
        initials[out++] = '?';
    }
    initials[out] = '\0';
    while (helikon_text_width(MUSIC_FONT_11M, initials) > size - 4u &&
           out > 1u) {
        initials[--out] = '\0';
    }
    int32_t baseline = (int32_t)(y + size / 2u + 5u);
    helikon_text_draw_centered(target, (int32_t)(x + size / 2u), baseline,
                               MUSIC_FONT_11M, MUSIC_INK, initials);
}

static void draw_sleeve_placeholder(struct helikon_screen *target, uint32_t x,
                                    uint32_t y, uint32_t size)
{
    helikon_screen_fill_rect(target, x, y, size, size, MUSIC_PAPER_LIGHT);
    if (size < 12u)
        return;
    helikon_screen_fill_rect(target, x, y, size, 1u, MUSIC_RULE);
    helikon_screen_fill_rect(target, x, y + size - 1u, size, 1u, MUSIC_RULE);
    helikon_screen_fill_rect(target, x, y, 1u, size, MUSIC_RULE);
    helikon_screen_fill_rect(target, x + size - 1u, y, 1u, size, MUSIC_RULE);

    int32_t left = (int32_t)x;
    int32_t top = (int32_t)y;
    int32_t span = (int32_t)size;
    draw_circle_outline(target, left + span * 7 / 10, top + span * 24 / 100,
                        span / 10, MUSIC_MUTED);
    draw_line(target, left + span * 8 / 100, top + span * 66 / 100,
              left + span * 34 / 100, top + span * 38 / 100, MUSIC_MUTED);
    draw_line(target, left + span * 34 / 100, top + span * 38 / 100,
              left + span * 58 / 100, top + span * 66 / 100, MUSIC_MUTED);
    draw_line(target, left + span * 6 / 100, top + span * 68 / 100,
              left + span * 94 / 100, top + span * 68 / 100, MUSIC_MUTED);
}

static int draw_album_art(struct helikon_screen *target, uint32_t artwork_id,
                          uint32_t x, uint32_t y, uint32_t size,
                          uint32_t flags)
{
    if ((flags & MUSIC_PREVIEW_NO_ART) == 0u && load_art(artwork_id) != 0) {
        draw_scaled_art(target, x, y, size, MUSIC_ART_SIZE);
        return 1;
    }
    draw_sleeve_placeholder(target, x, y, size);
    return 0;
}

static int draw_artist_art(struct helikon_screen *target, uint32_t image_id,
                           const char *name, uint32_t x, uint32_t y,
                           uint32_t size, uint32_t flags)
{
    if ((flags & MUSIC_PREVIEW_NO_ART) == 0u && load_art(image_id) != 0) {
        draw_scaled_art(target, x, y, size, MUSIC_ART_SIZE);
        return 1;
    }
    draw_plex_monogram(target, x, y, size, name);
    return 0;
}

static void draw_rating(struct helikon_screen *target, uint32_t right,
                        uint32_t baseline, uint16_t rating)
{
    uint32_t filled = rating == 0u ? 0u : (uint32_t)((rating + 10u) / 20u);
    if (filled > 5u)
        filled = 5u;
    uint32_t start = right - 34u;
    for (uint32_t index = 0u; index < 5u; ++index) {
        int32_t cx = (int32_t)(start + 3u + index * 7u);
        int32_t cy = (int32_t)baseline;
        if (index < filled)
            draw_diamond_filled(target, cx, cy, 3, MUSIC_INK);
        else
            draw_diamond_outline(target, cx, cy, 3, MUSIC_RULE);
    }
}

static void draw_play_all(struct helikon_screen *target, int focused)
{
    uint32_t x = MUSIC_X;
    uint32_t y = 114u;
    uint32_t width = 72u;
    uint32_t height = 18u;
    if (focused) {
        helikon_screen_fill_rect(target, x, y, width, height, MUSIC_INK);
    } else {
        helikon_screen_fill_rect(target, x, y, width, 1u, MUSIC_INK);
        helikon_screen_fill_rect(target, x, y + height - 1u, width, 1u,
                                 MUSIC_INK);
        helikon_screen_fill_rect(target, x, y, 1u, height, MUSIC_INK);
        helikon_screen_fill_rect(target, x + width - 1u, y, 1u, height,
                                 MUSIC_INK);
    }
    uint16_t ink = focused ? MUSIC_PAPER : MUSIC_INK;
    draw_play_triangle(target, (int32_t)x + 7, (int32_t)y + 5, 8, 8, ink);
    helikon_text_draw(target, (int32_t)x + 20,
                      (int32_t)(y + height - 5u), MUSIC_FONT_11M, ink,
                      "PLAY ALL", 50u);
}

#ifdef HELIKON_AUDIO_EMBEDDED
static void draw_visualizer(struct helikon_screen *target, uint32_t x,
                            uint32_t y, uint32_t width, uint32_t height,
                            const uint8_t *peaks, int active, uint8_t mode)
{
    if (!active || peaks == NULL || mode >= 2u) {
        helikon_screen_fill_rect(target, x, y + height, width, 1u, MUSIC_FAINT);
        return;
    }
    if (mode == 1u) {
        for (uint32_t index = 0u; index < 4u; ++index) {
            uint32_t level = 0u;
            for (uint32_t sample = index * 3u; sample < index * 3u + 3u;
                 ++sample) {
                if (peaks[sample] > level)
                    level = peaks[sample];
            }
            if (level > 15u)
                level = 15u;
            uint32_t bx = x + 2u + index * 24u;
            uint32_t bar = level * (height - 1u) / 15u;
            if (bar == 0u)
                bar = 1u;
            helikon_screen_fill_rect(target, bx, y + height - bar, 8u, bar,
                                     MUSIC_INK);
        }
    } else {
        for (uint32_t index = 0u; index < 12u; ++index) {
            uint32_t bx = x + 2u + index * 8u;
            uint32_t level = peaks[index] > 15u ? 15u : peaks[index];
            uint32_t bar = level * (height - 1u) / 15u;
            if (bar == 0u)
                bar = 1u;
            helikon_screen_fill_rect(target, bx, y + height - bar, 4u, bar,
                                     MUSIC_INK);
        }
    }
    helikon_screen_fill_rect(target, x, y + height, width, 1u, MUSIC_FAINT);
}
#endif

#ifdef HELIKON_AUDIO_EMBEDDED
static void draw_progress(struct helikon_screen *target, uint32_t position_ms,
                          uint32_t duration_ms)
{
    uint32_t y = 166u;
    helikon_screen_fill_rect(target, MUSIC_X, y, MUSIC_COLUMN, 4u, MUSIC_RULE);
    if (duration_ms == 0u)
        return;
    if (position_ms > duration_ms)
        position_ms = duration_ms;
    uint32_t width = (uint32_t)(((uint64_t)position_ms * MUSIC_COLUMN) /
                                duration_ms);
    if (position_ms != 0u && width == 0u)
        width = 1u;
    if (width != 0u)
        helikon_screen_fill_rect(target, MUSIC_X, y, width, 4u, MUSIC_INK);
}
#endif

#ifdef HELIKON_AUDIO_EMBEDDED
static void format_rate(char *buffer, uint32_t capacity, uint32_t rate)
{
    if (buffer == NULL || capacity == 0u)
        return;
    if (rate == 0u) {
        copy_text(buffer, capacity, "\xe2\x80\x94");
        return;
    }
    copy_text(buffer, capacity, "");
    append_u32(buffer, capacity, rate / 1000u);
    uint32_t remainder = (rate % 1000u) / 100u;
    if (remainder != 0u) {
        append_text(buffer, capacity, ".");
        append_u32(buffer, capacity, remainder);
    }
    append_text(buffer, capacity, " kHz");
}
#endif

static void format_duration_short(uint32_t duration_ms, char *buffer,
                                  uint32_t capacity)
{
    if (buffer == NULL || capacity == 0u)
        return;
    if (duration_ms == 0u) {
        copy_text(buffer, capacity, "\xe2\x80\x94");
        return;
    }
    uint32_t seconds = duration_ms / 1000u;
    copy_text(buffer, capacity, "");
    append_u32(buffer, capacity, seconds / 60u);
    append_text(buffer, capacity, ":");
    uint32_t rest = seconds % 60u;
    if (rest < 10u)
        append_text(buffer, capacity, "0");
    append_u32(buffer, capacity, rest);
}

#ifdef HELIKON_AUDIO_EMBEDDED
static void format_time(uint32_t milliseconds, char *buffer, uint32_t capacity)
{
    if (buffer == NULL || capacity == 0u)
        return;
    uint32_t seconds = milliseconds / 1000u;
    uint32_t hours = seconds / 3600u;
    uint32_t minutes = (seconds % 3600u) / 60u;
    uint32_t rest = seconds % 60u;
    copy_text(buffer, capacity, "");
    if (hours != 0u) {
        append_u32(buffer, capacity, hours);
        append_text(buffer, capacity, ":");
        if (minutes < 10u)
            append_text(buffer, capacity, "0");
    }
    append_u32(buffer, capacity, minutes);
    append_text(buffer, capacity, ":");
    if (rest < 10u)
        append_text(buffer, capacity, "0");
    append_u32(buffer, capacity, rest);
}
#endif

static void format_position(char *buffer, uint32_t capacity, uint32_t first,
                            uint32_t last, uint32_t total)
{
    if (buffer == NULL || capacity == 0u)
        return;
    if (total == 0u) {
        copy_text(buffer, capacity, "0 / 0");
        return;
    }
    copy_text(buffer, capacity, "");
    append_u32(buffer, capacity, first);
    append_text(buffer, capacity, "\xe2\x80\x93");
    append_u32(buffer, capacity, last);
    append_text(buffer, capacity, " / ");
    append_u32(buffer, capacity, total);
}

static void format_album_count(char *buffer, uint32_t capacity, uint32_t count)
{
    copy_text(buffer, capacity, "");
    append_u32(buffer, capacity, count);
    append_text(buffer, capacity, count == 1u ? " ALBUM" : " ALBUMS");
}

static void format_artist_counts(char *buffer, uint32_t capacity,
                                 const struct hdb_artist *artist)
{
    if (artist == NULL || artist->album_count == HDB_NO_ID ||
        artist->track_count == HDB_NO_ID) {
        copy_text(buffer, capacity, "COUNTS UNAVAILABLE");
        return;
    }
    if (artist->album_count == 0u) {
        copy_text(buffer, capacity, "NO ALBUMS");
        return;
    }
    copy_text(buffer, capacity, "");
    append_u32(buffer, capacity, artist->album_count);
    append_text(buffer, capacity,
                artist->album_count == 1u ? " ALBUM \xc2\xb7 " : " ALBUMS \xc2\xb7 ");
    append_u32(buffer, capacity, artist->track_count);
    append_text(buffer, capacity,
                artist->track_count == 1u ? " TRACK" : " TRACKS");
}

static char artist_group_letter(const char *sort)
{
    if (sort == NULL || sort[0] == '\0')
        return '#';
    uint8_t value = (uint8_t)sort[0];
    if (value >= 'a' && value <= 'z')
        return (char)(value - 'a' + 'A');
    if (value >= 'A' && value <= 'Z')
        return (char)value;
    return '#';
}




static uint32_t album_duration_ms(const struct hdb_album *album)
{
    uint32_t total = 0u;
    for (uint32_t index = 0u; index < album->track_count; ++index) {
        struct hdb_track track;
        if (hdb_track(&db, album->first_track + index, &track) != HELIKON_OK)
            continue;
        if (track.duration_ms > 0xffffffffu - total)
            return 0xffffffffu;
        total += track.duration_ms;
    }
    return total;
}

static int track_has_audio(uint32_t track_id)
{
#ifdef HELIKON_AUDIO_EMBEDDED
    return track_id == (uint32_t)HELIKON_AUDIO_TRACK;
#else
    (void)track_id;
    return 0;
#endif
}

static int album_track_playable(const struct hdb_album *album,
                                uint32_t index, uint32_t flags)
{
    if (index >= album->track_count)
        return 0;
    if ((flags & MUSIC_PREVIEW_CORRUPT) != 0u &&
        (index == 1u || (album->track_count == 1u && index == 0u)))
        return 0;
    if ((flags & MUSIC_PREVIEW_ALL_PLAYABLE) != 0u)
        return 1;
    return track_has_audio(album->first_track + index);
}

static int32_t first_playable_album_track(const struct hdb_album *album,
                                          uint32_t flags)
{
    for (uint32_t index = 0u; index < album->track_count; ++index) {
        if (album_track_playable(album, index, flags))
            return (int32_t)index;
    }
    return -1;
}

static uint32_t album_playable_count(const struct hdb_album *album,
                                     uint32_t flags)
{
    uint32_t count = 0u;
    for (uint32_t index = 0u; index < album->track_count; ++index) {
        if (album_track_playable(album, index, flags))
            ++count;
    }
    return count;
}

static int32_t next_playable_album_track(const struct hdb_album *album,
                                         int32_t focus, int delta,
                                         uint32_t flags)
{
    uint32_t playable = album_playable_count(album, flags);
    if (playable == 0u)
        return -1;
    if (focus < 0) {
        if (delta > 0)
            return first_playable_album_track(album, flags);
        return -1;
    }
    if (delta > 0) {
        for (uint32_t index = (uint32_t)focus + 1u;
             index < album->track_count; ++index) {
            if (album_track_playable(album, index, flags))
                return (int32_t)index;
        }
        return focus;
    }
    for (uint32_t index = (uint32_t)focus; index > 0u; --index) {
        if (album_track_playable(album, index - 1u, flags))
            return (int32_t)(index - 1u);
    }
    return -1;
}

static uint32_t artist_playable_count(uint32_t artist_id)
{
    uint32_t count = 0u;
    for (uint32_t index = 0u; index < artist_track_count(artist_id); ++index) {
        uint32_t track_id = 0u;
        if (artist_track_at(artist_id, index, &track_id) == HELIKON_OK &&
            track_has_audio(track_id))
            ++count;
    }
    return count;
}

static int32_t artist_first_playable(uint32_t artist_id)
{
    uint32_t count = artist_track_count(artist_id);
    for (uint32_t index = 0u; index < count; ++index) {
        uint32_t track_id = 0u;
        if (artist_track_at(artist_id, index, &track_id) == HELIKON_OK &&
            track_has_audio(track_id))
            return (int32_t)index;
    }
    return -1;
}

static int32_t artist_next_playable(uint32_t artist_id, int32_t focus,
                                    int delta)
{
    uint32_t count = artist_track_count(artist_id);
    if (artist_playable_count(artist_id) == 0u)
        return -1;
    if (focus < 0) {
        if (delta > 0)
            return artist_first_playable(artist_id);
        return -1;
    }
    if (delta > 0) {
        for (uint32_t index = (uint32_t)focus + 1u; index < count; ++index) {
            uint32_t track_id = 0u;
            if (artist_track_at(artist_id, index, &track_id) == HELIKON_OK &&
                track_has_audio(track_id))
                return (int32_t)index;
        }
        return focus;
    }
    for (uint32_t index = (uint32_t)focus; index > 0u; --index) {
        uint32_t track_id = 0u;
        if (artist_track_at(artist_id, index - 1u, &track_id) == HELIKON_OK &&
            track_has_audio(track_id))
            return (int32_t)(index - 1u);
    }
    return -1;
}

static void draw_artists_list_screen(struct helikon_screen *target,
                                     uint32_t selected, uint32_t top,
                                     uint32_t flags)
{
    static const char *const long_names[4] = {
        "The Extraordinarily Long Name Of A Contemporary Chamber Ensemble",
        "A Band With A Very Long Name That Must Be Truncated In The List",
        "Cinder Choir",
        "Dunmore Ensemble",
    };
    static const char *const long_counts[4] = {
        "NO ALBUMS",
        "COUNTS UNAVAILABLE",
        "NO ALBUMS",
        "7 ALBUMS \xc2\xb7 63 TRACKS",
    };

    helikon_screen_fill_rect(target, 0u, 0u, target->width, target->height,
                             MUSIC_PAPER);
    draw_top_bar(target, "MUSIC");
    helikon_text_draw(target, (int32_t)MUSIC_X, 50, MUSIC_FONT_17, MUSIC_INK,
                      "ARTISTS", 220u);

    uint32_t count = (flags & MUSIC_PREVIEW_EMPTY) != 0u
                         ? 0u
                         : ((flags & MUSIC_PREVIEW_LONG) != 0u ? 78u
                                                              : db.artist_count);
    char count_text[32];
    if (count == 1u)
        copy_text(count_text, sizeof(count_text), "1 ARTIST");
    else {
        copy_text(count_text, sizeof(count_text), "");
        append_u32(count_text, sizeof(count_text), count);
        append_text(count_text, sizeof(count_text), " ARTISTS");
    }
    helikon_text_draw_right(target, (int32_t)MUSIC_RIGHT, 50, MUSIC_FONT_11,
                            MUSIC_MUTED, count_text, 90u);
    draw_rule(target, 58u, MUSIC_RULE);

    char section[2] = { '#', '\0' };
    if (count != 0u) {
        if ((flags & MUSIC_PREVIEW_LONG) != 0u)
            section[0] = 'A';
        else {
            struct hdb_artist artist;
            char sort[128];
            sort[0] = '\0';
            if (hdb_artist(&db, top, &artist) == HELIKON_OK)
                (void)read_string(artist.sort_off, sort, sizeof(sort));
            section[0] = artist_group_letter(sort);
        }
        helikon_text_draw(target, (int32_t)MUSIC_X, 72, MUSIC_FONT_11M,
                          MUSIC_INK, section, 20u);
    }
    helikon_text_draw_right(target, (int32_t)MUSIC_RIGHT, 72, MUSIC_FONT_11,
                            MUSIC_MUTED, "SORT: NAME", 100u);

    if (count == 0u) {
        helikon_text_draw(target, (int32_t)MUSIC_X, 96, MUSIC_FONT_11,
                          MUSIC_MUTED, "NO ARTISTS IN LIBRARY", 288u);
    }

    for (uint32_t slot = 0u; slot < 4u; ++slot) {
        uint32_t index = top + slot;
        if (index >= count)
            break;
        uint32_t y = 78u + slot * 36u;
        int selected_row = index == selected;
        if (selected_row)
            helikon_screen_fill_rect(target, MUSIC_X, y, 2u, 34u, MUSIC_INK);

        char name[128];
        char counts[64];
        uint32_t artwork_id = HDB_NO_ID;
        int has_record = 0;
        int name_empty = 0;
        if ((flags & MUSIC_PREVIEW_LONG) != 0u) {
            copy_text(name, sizeof(name), long_names[slot]);
            copy_text(counts, sizeof(counts), long_counts[slot]);
        } else {
            struct hdb_artist artist;
            if (hdb_artist(&db, index, &artist) == HELIKON_OK) {
                has_record = 1;
                if (read_string(artist.name_off, name, sizeof(name)) !=
                        HELIKON_OK ||
                    name[0] == '\0') {
                    copy_text(name, sizeof(name), "UNKNOWN ARTIST");
                    name_empty = 1;
                }
                artwork_id = artist.image_id;
                format_artist_counts(counts, sizeof(counts), &artist);
            } else {
                copy_text(name, sizeof(name), "UNKNOWN ARTIST");
                copy_text(counts, sizeof(counts), "COUNTS UNAVAILABLE");
                name_empty = 1;
            }
        }

        helikon_screen_fill_rect(target, 24u, y + 3u, 28u, 28u, MUSIC_PAPER);
        helikon_screen_fill_rect(target, 24u, y + 3u, 28u, 1u,
                                 selected_row ? MUSIC_INK : MUSIC_RULE);
        helikon_screen_fill_rect(target, 24u, y + 30u, 28u, 1u,
                                 selected_row ? MUSIC_INK : MUSIC_RULE);
        helikon_screen_fill_rect(target, 24u, y + 3u, 1u, 28u,
                                 selected_row ? MUSIC_INK : MUSIC_RULE);
        helikon_screen_fill_rect(target, 51u, y + 3u, 1u, 28u,
                                 selected_row ? MUSIC_INK : MUSIC_RULE);
        (void)draw_artist_art(target, artwork_id, name_empty ? "" : name,
                              25u, y + 4u, 26u,
                              flags | ((flags & MUSIC_PREVIEW_LONG) != 0u
                                           ? MUSIC_PREVIEW_NO_ART
                                           : 0u));

        helikon_text_draw(target, 60, (int32_t)(y + 14u),
                          selected_row ? MUSIC_FONT_14M : MUSIC_FONT_14,
                          MUSIC_INK, name, 244u);
        helikon_text_draw(target, 60, (int32_t)(y + 28u), MUSIC_FONT_11,
                          selected_row ? MUSIC_INK : MUSIC_MUTED, counts, 244u);
        (void)has_record;
    }

    draw_rule(target, 226u, MUSIC_FAINT);
    helikon_text_draw(target, (int32_t)MUSIC_X, 236, MUSIC_FONT_11, MUSIC_MUTED,
                      "WHEEL SCROLL", 150u);
    char range[32];
    if (count == 0u)
        copy_text(range, sizeof(range), "0 / 0");
    else {
        uint32_t last = top + 4u;
        if (last > count)
            last = count;
        format_position(range, sizeof(range), top + 1u, last, count);
    }
    helikon_text_draw_right(target, (int32_t)MUSIC_RIGHT, 236, MUSIC_FONT_11,
                            MUSIC_MUTED, range, 80u);
}

static void draw_artist_page(struct helikon_screen *target, uint32_t artist_id,
                             uint32_t tab, uint32_t album_selected,
                             uint32_t track_selected, uint32_t flags)
{
    helikon_screen_fill_rect(target, 0u, 0u, target->width, target->height,
                             MUSIC_PAPER);
    draw_top_bar(target, "ARTISTS");

    struct hdb_artist artist;
    if (hdb_artist(&db, artist_id, &artist) != HELIKON_OK) {
        helikon_text_draw(target, (int32_t)MUSIC_X, 60, MUSIC_FONT_12,
                          MUSIC_MUTED, "MISSING ARTIST", 288u);
        return;
    }

    char name[128];
    char sort[128];
    char genre[96];
    char bio[192];
    name[0] = '\0';
    sort[0] = '\0';
    genre[0] = '\0';
    bio[0] = '\0';
    (void)read_string(artist.name_off, name, sizeof(name));
    (void)read_string(artist.sort_off, sort, sizeof(sort));
    (void)read_string(artist.bio_off, bio, sizeof(bio));
    int name_empty = name[0] == '\0';
    if (name_empty)
        copy_text(name, sizeof(name), "UNKNOWN ARTIST");
    artist_genre(artist_id, genre, sizeof(genre));

    uint32_t album_count = artist.album_count;
    uint32_t track_count = artist.track_count;
    if ((flags & MUSIC_PREVIEW_EMPTY) != 0u) {
        album_count = 0u;
        track_count = 0u;
        bio[0] = '\0';
    }
    if (album_count != 0u && album_selected >= album_count)
        album_selected = album_count - 1u;
    if (track_count != 0u && track_selected >= track_count)
        track_selected = track_count - 1u;

    helikon_text_draw(target, (int32_t)MUSIC_X, 48, MUSIC_FONT_17, MUSIC_INK,
                      name, 228u);
    helikon_text_draw(target, (int32_t)MUSIC_X, 64, MUSIC_FONT_12, MUSIC_MUTED,
                      genre[0] != '\0' ? genre : "\xe2\x80\x94", 228u);
    char counts[64];
    format_artist_counts(counts, sizeof(counts), &artist);
    if ((flags & MUSIC_PREVIEW_EMPTY) != 0u)
        copy_text(counts, sizeof(counts), "NO ALBUMS");
    helikon_text_draw(target, (int32_t)MUSIC_X, 78, MUSIC_FONT_11, MUSIC_MUTED,
                      counts, 228u);

    helikon_screen_fill_rect(target, 256u, 32u, 44u, 44u, MUSIC_PAPER);
    helikon_screen_fill_rect(target, 256u, 32u, 44u, 1u, MUSIC_RULE);
    helikon_screen_fill_rect(target, 256u, 75u, 44u, 1u, MUSIC_RULE);
    helikon_screen_fill_rect(target, 256u, 32u, 1u, 44u, MUSIC_RULE);
    helikon_screen_fill_rect(target, 299u, 32u, 1u, 44u, MUSIC_RULE);
    (void)draw_artist_art(target, artist.image_id, name_empty ? "" : name,
                          257u, 33u, 42u, flags);

    draw_rule(target, 84u, MUSIC_RULE);
    static const char *const tab_labels[3] = { "ALBUMS", "OVERVIEW", "TRACKS" };
    static const uint32_t tab_x[3] = { 16u, 84u, 168u };
    if (tab > 2u)
        tab = 0u;
    for (uint32_t index = 0u; index < 3u; ++index) {
        uint8_t style = index == tab ? MUSIC_FONT_11M : MUSIC_FONT_11;
        uint16_t color = index == tab ? MUSIC_INK : MUSIC_MUTED;
        helikon_text_draw(target, (int32_t)tab_x[index], 98, style, color,
                          tab_labels[index], 70u);
    }
    helikon_screen_fill_rect(target, MUSIC_X, 101u, MUSIC_COLUMN, 1u,
                             MUSIC_FAINT);
    uint32_t underline = helikon_text_width(MUSIC_FONT_11M,
                                            tab_labels[tab]);
    helikon_screen_fill_rect(target, tab_x[tab], 102u, underline, 2u,
                             MUSIC_INK);
    char albums_label[32];
    format_album_count(albums_label, sizeof(albums_label), album_count);
    helikon_text_draw_right(target, (int32_t)MUSIC_RIGHT, 98, MUSIC_FONT_11,
                            MUSIC_MUTED, albums_label, 90u);

    if (tab == 0u) {
        char position[32];
        if (album_count == 0u) {
            copy_text(position, sizeof(position), "0 / 0");
            helikon_text_draw(target, (int32_t)MUSIC_X, 128, MUSIC_FONT_11,
                              MUSIC_MUTED, "NO ALBUMS IN LIBRARY", 288u);
        } else {
            uint32_t page = album_selected / 3u;
            uint32_t first = page * 3u + 1u;
            uint32_t last = page * 3u + 3u;
            if (last > album_count)
                last = album_count;
            format_position(position, sizeof(position), first, last,
                            album_count);
            for (uint32_t slot = 0u; slot < 3u; ++slot) {
                uint32_t index = page * 3u + slot;
                if (index >= album_count)
                    break;
                uint32_t album_id = 0u;
                if (artist_album_at(artist_id, index, &album_id) != HELIKON_OK)
                    continue;
                struct hdb_album album;
                if (hdb_album(&db, album_id, &album) != HELIKON_OK)
                    continue;
                char title[128];
                (void)read_string(album.title_off, title, sizeof(title));
                if (title[0] == '\0')
                    copy_text(title, sizeof(title), "UNTITLED ALBUM");
                if ((flags & MUSIC_PREVIEW_LONG) != 0u && index == 0u)
                    copy_text(title, sizeof(title),
                              "The Extraordinarily Long Album Title That Needs "
                              "Two Lines");
                uint32_t x = 16u + slot * 108u;
                int selected_album = index == album_selected;
                helikon_screen_fill_rect(target, x, 118u, 72u, 72u, MUSIC_PAPER);
                uint32_t frame = selected_album ? 2u : 1u;
                helikon_screen_fill_rect(target, x, 118u, 72u, frame,
                                         selected_album ? MUSIC_INK
                                                        : MUSIC_RULE);
                helikon_screen_fill_rect(target, x, 190u - frame, 72u, frame,
                                         selected_album ? MUSIC_INK
                                                        : MUSIC_RULE);
                helikon_screen_fill_rect(target, x, 118u, frame, 72u,
                                         selected_album ? MUSIC_INK
                                                        : MUSIC_RULE);
                helikon_screen_fill_rect(target, x + 72u - frame, 118u, frame,
                                         72u,
                                         selected_album ? MUSIC_INK
                                                        : MUSIC_RULE);
                (void)draw_album_art(target, album.artwork_id, x + 2u, 120u,
                                     68u, flags);
                helikon_text_draw_wrapped(
                    target, (int32_t)x, 204,
                    MUSIC_FONT_12, MUSIC_INK, title, 72u, 2u, 14u);
                char year[24];
                if (album.year == 0u)
                    copy_text(year, sizeof(year), "YEAR \xe2\x80\x94");
                else {
                    copy_text(year, sizeof(year), "");
                    append_u32(year, sizeof(year), album.year);
                }
                helikon_text_draw(target, (int32_t)x, 232, MUSIC_FONT_11,
                                  MUSIC_MUTED, year, 72u);
            }
        }
        helikon_text_draw_right(target, (int32_t)MUSIC_RIGHT, 112,
                                MUSIC_FONT_11, MUSIC_MUTED, position, 80u);
    } else if (tab == 1u) {
        if (bio[0] != '\0') {
            (void)helikon_text_draw_wrapped(target, (int32_t)MUSIC_X, 122,
                                            MUSIC_FONT_12, MUSIC_INK, bio,
                                            288u, 4u, 16u);
        } else {
            helikon_text_draw(target, (int32_t)MUSIC_X, 122, MUSIC_FONT_12,
                              MUSIC_MUTED, "NO BIOGRAPHY AVAILABLE", 288u);
        }
        if (album_count != 0u) {
            uint32_t first_id = 0u;
            uint32_t latest_id = 0u;
            char first_title[128];
            char latest_title[128];
            first_title[0] = '\0';
            latest_title[0] = '\0';
            struct hdb_album first_album;
            struct hdb_album latest_album;
            int have_first =
                artist_album_at(artist_id, 0u, &first_id) == HELIKON_OK &&
                hdb_album(&db, first_id, &first_album) == HELIKON_OK;
            int have_latest =
                artist_album_at(artist_id, album_count - 1u, &latest_id) ==
                    HELIKON_OK &&
                hdb_album(&db, latest_id, &latest_album) == HELIKON_OK;
            if (have_first)
                (void)read_string(first_album.title_off, first_title,
                                  sizeof(first_title));
            if (have_latest)
                (void)read_string(latest_album.title_off, latest_title,
                                  sizeof(latest_title));
            if (first_title[0] == '\0')
                copy_text(first_title, sizeof(first_title), "UNTITLED ALBUM");
            if (latest_title[0] == '\0')
                copy_text(latest_title, sizeof(latest_title), "UNTITLED ALBUM");
            draw_rule(target, 182u, MUSIC_FAINT);
            char facts[64];
            format_artist_counts(facts, sizeof(facts), &artist);
            helikon_text_draw(target, (int32_t)MUSIC_X, 198, MUSIC_FONT_11,
                              MUSIC_INK, facts, 288u);
            char line[192];
            copy_text(line, sizeof(line), "FIRST  ");
            append_text(line, sizeof(line), first_title);
            if (have_first && first_album.year != 0u) {
                append_text(line, sizeof(line), " (");
                append_u32(line, sizeof(line), first_album.year);
                append_text(line, sizeof(line), ")");
            }
            helikon_text_draw(target, (int32_t)MUSIC_X, 212, MUSIC_FONT_11,
                              MUSIC_MUTED, line, 288u);
            copy_text(line, sizeof(line), "LATEST  ");
            append_text(line, sizeof(line), latest_title);
            if (have_latest && latest_album.year != 0u) {
                append_text(line, sizeof(line), " (");
                append_u32(line, sizeof(line), latest_album.year);
                append_text(line, sizeof(line), ")");
            }
            helikon_text_draw(target, (int32_t)MUSIC_X, 226, MUSIC_FONT_11,
                              MUSIC_MUTED, line, 288u);
        } else if (bio[0] == '\0') {
            helikon_text_draw(target, (int32_t)MUSIC_X, 138, MUSIC_FONT_11,
                              MUSIC_MUTED, "NO CATALOG FACTS", 288u);
        } else {
            helikon_text_draw(target, (int32_t)MUSIC_X, 182, MUSIC_FONT_11,
                              MUSIC_MUTED, "NO CATALOG FACTS", 288u);
        }
    } else {
        char position[32];
        if (track_count == 0u) {
            copy_text(position, sizeof(position), "0 / 0");
            helikon_text_draw(target, (int32_t)MUSIC_X, 122, MUSIC_FONT_11,
                              MUSIC_MUTED, "NO TRACKS IN ARTIST", 288u);
        } else {
            uint32_t page = track_selected / 4u;
            uint32_t first = page * 4u + 1u;
            uint32_t last = page * 4u + 4u;
            if (last > track_count)
                last = track_count;
            format_position(position, sizeof(position), first, last,
                            track_count);
            for (uint32_t row = 0u; row < 4u; ++row) {
                uint32_t index = page * 4u + row;
                if (index >= track_count)
                    break;
                uint32_t track_id = 0u;
                if (artist_track_at(artist_id, index, &track_id) != HELIKON_OK)
                    continue;
                struct hdb_track track;
                if (hdb_track(&db, track_id, &track) != HELIKON_OK)
                    continue;
                char title[128];
                char album_name[128];
                title[0] = '\0';
                album_name[0] = '\0';
                (void)read_string(track.title_off, title, sizeof(title));
                if (title[0] == '\0')
                    copy_text(title, sizeof(title), "UNKNOWN TRACK");
                struct hdb_album album;
                if (hdb_album(&db, track.album_id, &album) == HELIKON_OK)
                    (void)read_string(album.title_off, album_name,
                                      sizeof(album_name));
                int playable = (flags & MUSIC_PREVIEW_ALL_PLAYABLE) != 0u ||
                               track_has_audio(track_id);
                int selected_track = index == track_selected && playable;
                uint32_t y = 118u + row * 28u;
                if (selected_track)
                    helikon_screen_fill_rect(target, MUSIC_X, y, 2u, 26u,
                                             MUSIC_INK);
                helikon_text_draw(target, 24, (int32_t)(y + 12u),
                                  selected_track ? MUSIC_FONT_14M
                                                 : MUSIC_FONT_14,
                                  playable ? MUSIC_INK : MUSIC_MUTED,
                                  playable ? title : "TRACK DATA MISSING",
                                  220u);
                if (playable)
                    helikon_text_draw(target, 24, (int32_t)(y + 24u),
                                      MUSIC_FONT_11, MUSIC_MUTED, album_name,
                                      220u);
                char duration[24];
                format_duration_short(track.duration_ms, duration,
                                      sizeof(duration));
                helikon_text_draw_right(target, (int32_t)MUSIC_RIGHT,
                                        (int32_t)(y + 12u), MUSIC_FONT_11,
                                        selected_track ? MUSIC_INK
                                                       : MUSIC_MUTED,
                                        playable ? duration : "", 46u);
            }
        }
        helikon_text_draw_right(target, (int32_t)MUSIC_RIGHT, 112,
                                MUSIC_FONT_11, MUSIC_MUTED, position, 80u);
    }
}

static void draw_album_page(struct helikon_screen *target, uint32_t album_id,
                            int32_t focus, uint32_t track_top, uint32_t flags)
{
    helikon_screen_fill_rect(target, 0u, 0u, target->width, target->height,
                             MUSIC_PAPER);
    draw_top_bar(target, "ALBUM");

    struct hdb_album album;
    if (hdb_album(&db, album_id, &album) != HELIKON_OK) {
        helikon_text_draw(target, (int32_t)MUSIC_X, 60, MUSIC_FONT_12,
                          MUSIC_MUTED, "MISSING ALBUM", 288u);
        return;
    }
    uint32_t track_count = (flags & MUSIC_PREVIEW_EMPTY) != 0u
                               ? 0u
                               : album.track_count;
    uint32_t playable = (flags & MUSIC_PREVIEW_EMPTY) != 0u
                            ? 0u
                            : album_playable_count(&album, flags);
    if (focus >= 0 && (uint32_t)focus >= track_count)
        focus = playable == 0u ? -1 : first_playable_album_track(&album, flags);
    if (focus >= 0 && !album_track_playable(&album, (uint32_t)focus, flags))
        focus = playable == 0u ? -1 : first_playable_album_track(&album, flags);
    if (focus < 0 && playable != 0u)
        focus = -1;
    if (track_top >= track_count && track_count != 0u)
        track_top = track_count - 1u;
    if (focus >= 0) {
        if ((uint32_t)focus < track_top)
            track_top = (uint32_t)focus;
        else if ((uint32_t)focus >= track_top + 3u)
            track_top = (uint32_t)focus - 2u;
    }

    helikon_screen_fill_rect(target, 16u, 36u, 72u, 72u, MUSIC_PAPER);
    helikon_screen_fill_rect(target, 16u, 36u, 72u, 1u, MUSIC_RULE);
    helikon_screen_fill_rect(target, 16u, 107u, 72u, 1u, MUSIC_RULE);
    helikon_screen_fill_rect(target, 16u, 36u, 1u, 72u, MUSIC_RULE);
    helikon_screen_fill_rect(target, 87u, 36u, 1u, 72u, MUSIC_RULE);
    (void)draw_album_art(target, album.artwork_id, 17u, 37u, 70u, flags);
    if (playable != 0u)
        draw_play_all(target, focus < 0);

    char year[24];
    if (album.year == 0u)
        copy_text(year, sizeof(year), "YEAR \xe2\x80\x94");
    else {
        copy_text(year, sizeof(year), "");
        append_u32(year, sizeof(year), album.year);
    }
    helikon_text_draw(target, 100, 48, MUSIC_FONT_11, MUSIC_MUTED, year, 80u);
    draw_rating(target, MUSIC_RIGHT, 48u, album.rating);

    char title[128];
    (void)read_string(album.title_off, title, sizeof(title));
    if (title[0] == '\0')
        copy_text(title, sizeof(title), "UNTITLED ALBUM");
    if ((flags & MUSIC_PREVIEW_LONG) != 0u)
        copy_text(title, sizeof(title),
                  "The Extraordinarily Long Album Title That Needs Two Lines");
    (void)helikon_text_draw_wrapped(target, 100, 68, MUSIC_FONT_17, MUSIC_INK,
                                    title, 204u, 2u, 20u);

    char artist[128];
    artist[0] = '\0';
    struct hdb_artist artist_record;
    if (hdb_artist(&db, album.artist_id, &artist_record) == HELIKON_OK)
        (void)read_string(artist_record.name_off, artist, sizeof(artist));
    if (artist[0] == '\0')
        copy_text(artist, sizeof(artist), "UNKNOWN ARTIST");
    if ((flags & MUSIC_PREVIEW_LONG) != 0u)
        copy_text(artist, sizeof(artist),
                  "The Extraordinarily Long Artist Name");
    helikon_text_draw(target, 100, 104, MUSIC_FONT_12, MUSIC_INK, artist, 204u);

    char genre[96];
    genre[0] = '\0';
    (void)read_string(album.genre_off, genre, sizeof(genre));
    char meta[160];
    meta[0] = '\0';
    if (genre[0] != '\0')
        append_text(meta, sizeof(meta), genre);
    uint32_t duration = album_duration_ms(&album);
    if (duration != 0u) {
        if (meta[0] != '\0')
            append_text(meta, sizeof(meta), " \xc2\xb7 ");
        append_u32(meta, sizeof(meta), (duration + 30000u) / 60000u);
        append_text(meta, sizeof(meta), " MIN");
    }
    if (meta[0] == '\0')
        copy_text(meta, sizeof(meta), "\xe2\x80\x94");
    helikon_text_draw(target, 100, 118, MUSIC_FONT_11, MUSIC_MUTED, meta, 204u);

    draw_rule(target, 138u, MUSIC_RULE);
    helikon_text_draw(target, (int32_t)MUSIC_X, 150, MUSIC_FONT_11M, MUSIC_INK,
                      "TRACKS", 100u);
    char position[32];
    if (track_count == 0u) {
        copy_text(position, sizeof(position), "0 / 0");
        helikon_text_draw(target, (int32_t)MUSIC_X, 168, MUSIC_FONT_11,
                          MUSIC_MUTED, "NO TRACKS IN ALBUM", 288u);
    } else {
        uint32_t last = track_top + 3u;
        if (last > track_count)
            last = track_count;
        format_position(position, sizeof(position), track_top + 1u, last,
                        track_count);
    }
    helikon_text_draw_right(target, (int32_t)MUSIC_RIGHT, 150, MUSIC_FONT_11,
                            MUSIC_MUTED, position, 80u);

    if (album.disc_count > 1u && track_count != 0u) {
        uint32_t disc = 1u;
        uint32_t track_id = album.first_track +
                            (focus >= 0 ? (uint32_t)focus : track_top);
        struct hdb_track disc_track;
        if (hdb_track(&db, track_id, &disc_track) == HELIKON_OK &&
            disc_track.disc_no != 0u)
            disc = disc_track.disc_no;
        char disc_label[24];
        copy_text(disc_label, sizeof(disc_label), "DISC ");
        append_u32(disc_label, sizeof(disc_label), disc);
        helikon_text_draw(target, 100, 150, MUSIC_FONT_11, MUSIC_MUTED,
                          disc_label, 80u);
    }

    if (track_count != 0u) {
        for (uint32_t row = 0u; row < 3u; ++row) {
            uint32_t index = track_top + row;
            if (index >= track_count)
                break;
            struct hdb_track track;
            if (hdb_track(&db, album.first_track + index, &track) !=
                HELIKON_OK)
                continue;
            int track_playable = album_track_playable(&album, index, flags);
            int selected_track = focus >= 0 && (uint32_t)focus == index &&
                                 track_playable;
            uint32_t y = 158u + row * 18u;
            if (selected_track)
                helikon_screen_fill_rect(target, MUSIC_X, y, 2u, 18u,
                                         MUSIC_INK);
            char number[8];
            if (track_playable) {
                uint32_t value = track.track_no != 0u ? track.track_no
                                                      : index + 1u;
                if (value < 10u)
                    copy_text(number, sizeof(number), "0");
                else
                    copy_text(number, sizeof(number), "");
                append_u32(number, sizeof(number), value);
            } else {
                copy_text(number, sizeof(number), "\xe2\x80\x94");
            }
            helikon_text_draw(target, (int32_t)MUSIC_X + 4, (int32_t)(y + 13u),
                              MUSIC_FONT_11,
                              selected_track ? MUSIC_INK : MUSIC_MUTED,
                              number, 22u);
            char track_title[128];
            track_title[0] = '\0';
            if (track_playable) {
                (void)read_string(track.title_off, track_title,
                                  sizeof(track_title));
                if (track_title[0] == '\0')
                    copy_text(track_title, sizeof(track_title), "UNKNOWN TRACK");
                if ((flags & MUSIC_PREVIEW_LONG) != 0u && index == 0u)
                    copy_text(track_title, sizeof(track_title),
                              "An Extraordinarily Long Track Title That Needs "
                              "An Ellipsis");
            } else {
                copy_text(track_title, sizeof(track_title),
                          "TRACK DATA MISSING");
            }
            helikon_text_draw(target, (int32_t)MUSIC_X + 22,
                              (int32_t)(y + 13u),
                              selected_track ? MUSIC_FONT_14M : MUSIC_FONT_14,
                              track_playable ? MUSIC_INK : MUSIC_MUTED,
                              track_title, 220u);
            if (track_playable) {
                char track_duration[24];
                format_duration_short(track.duration_ms, track_duration,
                                      sizeof(track_duration));
                helikon_text_draw_right(
                    target, (int32_t)MUSIC_RIGHT, (int32_t)(y + 13u),
                    MUSIC_FONT_11,
                    selected_track ? MUSIC_INK : MUSIC_MUTED, track_duration,
                    46u);
            }
        }
    }

    draw_rule(target, 216u, MUSIC_FAINT);
    uint32_t unreadable = track_count - playable;
    char status[64];
    status[0] = '\0';
    if (track_count == 0u) {
        if ((flags & MUSIC_PREVIEW_CREDITS) != 0u) {
            helikon_text_draw(target, (int32_t)MUSIC_X, 226, MUSIC_FONT_11M,
                              MUSIC_INK, "NOTES", 40u);
            helikon_text_draw(target, 58, 226, MUSIC_FONT_11, MUSIC_MUTED,
                              "Recorded in one room over three nights.", 246u);
            helikon_text_draw(target, (int32_t)MUSIC_X, 236, MUSIC_FONT_11M,
                              MUSIC_INK, "CREDITS", 50u);
            helikon_text_draw(target, 68, 236, MUSIC_FONT_11, MUSIC_MUTED,
                              "A. Voss, producer \xc2\xb7 M. Rill, engineer",
                              236u);
        } else {
            helikon_text_draw(target, (int32_t)MUSIC_X, 228, MUSIC_FONT_11,
                              MUSIC_MUTED, "NO CREDITS LISTED", 288u);
        }
    } else if (playable == 0u) {
        helikon_text_draw(target, (int32_t)MUSIC_X, 228, MUSIC_FONT_11,
                          MUSIC_MUTED, "NO PLAYABLE TRACKS", 288u);
    } else if (unreadable != 0u) {
        copy_text(status, sizeof(status), "");
        append_u32(status, sizeof(status), unreadable);
        append_text(status, sizeof(status),
                    unreadable == 1u ? " TRACK UNREADABLE"
                                     : " TRACKS UNREADABLE");
        helikon_text_draw(target, (int32_t)MUSIC_X, 228, MUSIC_FONT_11,
                          MUSIC_MUTED, status, 288u);
    } else if ((flags & MUSIC_PREVIEW_CREDITS) != 0u) {
        helikon_text_draw(target, (int32_t)MUSIC_X, 226, MUSIC_FONT_11M,
                          MUSIC_INK, "NOTES", 40u);
        helikon_text_draw(target, 58, 226, MUSIC_FONT_11, MUSIC_MUTED,
                          "Recorded in one room over three nights.", 246u);
        helikon_text_draw(target, (int32_t)MUSIC_X, 236, MUSIC_FONT_11M,
                          MUSIC_INK, "CREDITS", 50u);
        helikon_text_draw(target, 68, 236, MUSIC_FONT_11, MUSIC_MUTED,
                          "A. Voss, producer \xc2\xb7 M. Rill, engineer", 236u);
    } else {
        char credits[128];
        credits[0] = '\0';
        for (uint32_t index = 0u; index < album.track_count; ++index) {
            struct hdb_track track;
            if (hdb_track(&db, album.first_track + index, &track) !=
                HELIKON_OK)
                continue;
            if (read_string(track.composer_off, credits, sizeof(credits)) ==
                    HELIKON_OK &&
                credits[0] != '\0')
                break;
        }
        if (credits[0] != '\0') {
            helikon_text_draw(target, (int32_t)MUSIC_X, 236, MUSIC_FONT_11M,
                              MUSIC_INK, "CREDITS", 50u);
            helikon_text_draw(target, 68, 236, MUSIC_FONT_11, MUSIC_MUTED,
                              credits, 236u);
        } else {
            helikon_text_draw(target, (int32_t)MUSIC_X, 228, MUSIC_FONT_11,
                              MUSIC_MUTED, "NO CREDITS LISTED", 288u);
        }
    }
}

enum now_playing_state {
    NOW_PLAYING_OPENING,
    NOW_PLAYING_PLAYING,
    NOW_PLAYING_PAUSED,
    NOW_PLAYING_FINISHED,
    NOW_PLAYING_ERROR
};

struct now_playing_data {
    enum now_playing_state state;
    uint32_t position_ms;
    uint32_t duration_ms;
    uint32_t stopped_ms;
    const uint8_t *peaks;
    int peaks_active;
    uint8_t visualizer_mode;
    int source_known;
    uint32_t source_rate;
    uint32_t source_channels;
    uint32_t source_bits;
    uint32_t source_bitrate_kbps;
    uint32_t output_rate;
    const char *container;
    const char *codec;
    const char *error_reason;
    int retryable;
    const char *status_override;
};

#ifdef HELIKON_AUDIO_EMBEDDED
static void format_source_line(char *buffer, uint32_t capacity,
                               const struct now_playing_data *data)
{
    if (!data->source_known) {
        copy_text(buffer, capacity, "SRC \xe2\x80\x94");
        return;
    }
    char rate[24];
    format_rate(rate, sizeof(rate), data->source_rate);
    copy_text(buffer, capacity, data->container != NULL ? data->container
                                                        : "\xe2\x80\x94");
    append_text(buffer, capacity, " \xc2\xb7 ");
    append_text(buffer, capacity, data->codec != NULL ? data->codec
                                                       : "\xe2\x80\x94");
    append_text(buffer, capacity, " \xc2\xb7 ");
    if (data->source_bits != 0u) {
        append_u32(buffer, capacity, data->source_bits);
        append_text(buffer, capacity, "-BIT");
    } else {
        append_text(buffer, capacity, "\xe2\x80\x94");
    }
    append_text(buffer, capacity, " \xc2\xb7 ");
    append_text(buffer, capacity, rate);
}

static void format_output_line(char *buffer, uint32_t capacity,
                               const struct now_playing_data *data)
{
    copy_text(buffer, capacity, "");
    if (!data->source_known) {
        copy_text(buffer, capacity, "OUT \xe2\x80\x94");
        return;
    }
    char rate[24];
    format_rate(rate, sizeof(rate), data->output_rate);
    if (data->source_bitrate_kbps != 0u) {
        append_u32(buffer, capacity, data->source_bitrate_kbps);
        append_text(buffer, capacity, " kbps");
    } else {
        copy_text(buffer, capacity, "\xe2\x80\x94");
    }
    append_text(buffer, capacity, " \xc2\xb7 OUT ");
    append_text(buffer, capacity, rate);
    append_text(buffer, capacity, " \xc2\xb7 STEREO");
}

static uint32_t track_number_for(uint32_t track_id)
{
    struct hdb_track track;
    if (hdb_track(&db, track_id, &track) != HELIKON_OK)
        return 0u;
    if (track.track_no != 0u)
        return track.track_no;
    struct hdb_album album;
    if (hdb_album(&db, track.album_id, &album) == HELIKON_OK &&
        track_id >= album.first_track)
        return track_id - album.first_track + 1u;
    return 0u;
}

static void format_track_line(char *buffer, uint32_t capacity,
                              uint32_t track_id, uint32_t duration_ms)
{
    uint32_t number = track_number_for(track_id);
    copy_text(buffer, capacity, "TRACK ");
    if (number == 0u)
        append_text(buffer, capacity, "\xe2\x80\x94");
    else
        append_u32(buffer, capacity, number);
    struct hdb_track track;
    if (hdb_track(&db, track_id, &track) == HELIKON_OK) {
        struct hdb_album album;
        if (hdb_album(&db, track.album_id, &album) == HELIKON_OK &&
            album.track_count != 0u) {
            append_text(buffer, capacity, " OF ");
            append_u32(buffer, capacity, album.track_count);
        }
    }
    if (duration_ms != 0u) {
        char duration[24];
        format_duration_short(duration_ms, duration, sizeof(duration));
        append_text(buffer, capacity, " \xc2\xb7 ");
        append_text(buffer, capacity, duration);
    }
}

static void draw_now_playing_view(struct helikon_screen *target,
                                  uint32_t track_id, uint32_t album_id,
                                  const struct now_playing_data *data,
                                  uint32_t flags)
{
    helikon_screen_fill_rect(target, 0u, 0u, target->width, target->height,
                             MUSIC_PAPER);
    draw_top_bar(target, "NOW PLAYING");

    char title[160];
    char artist[128];
    char album_name[128];
    title[0] = '\0';
    artist[0] = '\0';
    album_name[0] = '\0';
    struct hdb_track track;
    int have_track = hdb_track(&db, track_id, &track) == HELIKON_OK;
    if (have_track)
        (void)read_string(track.title_off, title, sizeof(title));
    if (title[0] == '\0')
        copy_text(title, sizeof(title), "UNKNOWN TRACK");
    struct hdb_album album;
    int have_album = hdb_album(&db, album_id, &album) == HELIKON_OK;
    if (have_album) {
        (void)read_string(album.title_off, album_name, sizeof(album_name));
        struct hdb_artist artist_record;
        if (hdb_artist(&db, album.artist_id, &artist_record) == HELIKON_OK)
            (void)read_string(artist_record.name_off, artist, sizeof(artist));
    }
    if (artist[0] == '\0')
        copy_text(artist, sizeof(artist), "UNKNOWN ARTIST");
    if (album_name[0] == '\0')
        copy_text(album_name, sizeof(album_name), "\xe2\x80\x94");
    if ((flags & MUSIC_PREVIEW_LONG) != 0u) {
        copy_text(title, sizeof(title),
                  "An Extraordinarily Long Track Title That Needs Two Lines And "
                  "An Ellipsis");
        copy_text(artist, sizeof(artist),
                  "The Extraordinarily Long Artist Name That Must Be Truncated");
        copy_text(album_name, sizeof(album_name),
                  "The Extraordinarily Long Album Title That Must Be Truncated");
    }

    if (data->state == NOW_PLAYING_ERROR) {
        helikon_screen_fill_rect(target, MUSIC_X, 40u, MUSIC_COLUMN, 2u,
                                 MUSIC_ERROR);
        helikon_text_draw(target, (int32_t)MUSIC_X, 68, MUSIC_FONT_17, MUSIC_INK,
                          "AUDIO PLAYBACK FAILED", 288u);
        char reason[64];
        copy_text(reason, sizeof(reason), data->error_reason != NULL
                                              ? data->error_reason
                                              : "AUDIO PLAYBACK STOPPED");
        size_t reason_length = 0u;
        while (reason[reason_length] != '\0')
            ++reason_length;
        if (reason_length > 44u) {
            reason[43] = '\0';
            append_text(reason, sizeof(reason), "\xe2\x80\xa6");
        }
        helikon_text_draw(target, (int32_t)MUSIC_X, 92, MUSIC_FONT_12, MUSIC_INK,
                          reason, 288u);
        helikon_text_draw(target, (int32_t)MUSIC_X, 114, MUSIC_FONT_12,
                          MUSIC_MUTED,
                          data->retryable ? "Press SELECT to retry."
                                          : "Return to the album.",
                          288u);
        char identity[320];
        copy_text(identity, sizeof(identity), title);
        append_text(identity, sizeof(identity), " \xe2\x80\x94 ");
        append_text(identity, sizeof(identity), artist);
        helikon_text_draw(target, (int32_t)MUSIC_X, 138, MUSIC_FONT_11,
                          MUSIC_MUTED, identity, 288u);
        draw_rule(target, 150u, MUSIC_FAINT);
        char source[128];
        format_source_line(source, sizeof(source), data);
        helikon_text_draw(target, (int32_t)MUSIC_X, 164, MUSIC_FONT_11,
                          MUSIC_MUTED, source, 288u);
        helikon_text_draw(target, (int32_t)MUSIC_X, 178, MUSIC_FONT_11,
                          MUSIC_MUTED, "OUT \xe2\x80\x94 \xc2\xb7 PLAYBACK STOPPED",
                          288u);
        char stopped[96];
        char stopped_time[24];
        format_time(data->stopped_ms, stopped_time, sizeof(stopped_time));
        copy_text(stopped, sizeof(stopped), "STOPPED AT ");
        append_text(stopped, sizeof(stopped), stopped_time);
        uint32_t number = have_track ? track_number_for(track_id) : 0u;
        if (number != 0u && have_album && album.track_count != 0u) {
            append_text(stopped, sizeof(stopped), " \xc2\xb7 TRACK ");
            append_u32(stopped, sizeof(stopped), number);
            append_text(stopped, sizeof(stopped), " OF ");
            append_u32(stopped, sizeof(stopped), album.track_count);
        }
        helikon_text_draw(target, (int32_t)MUSIC_X, 192, MUSIC_FONT_11,
                          MUSIC_MUTED, stopped, 288u);
        const char *footer;
        if (data->retryable)
            footer = "SELECT RETRY \xc2\xb7 MENU BACK";
        else if (have_album)
            footer = "SELECT ALBUM \xc2\xb7 MENU BACK";
        else
            footer = "MENU BACK";
        helikon_text_draw(target, (int32_t)MUSIC_X, 232, MUSIC_FONT_11,
                          MUSIC_MUTED, footer, 288u);
        return;
    }

    uint32_t artwork_id = HDB_NO_ID;
    if (have_track)
        artwork_id = track.artwork_id;
    if (artwork_id == HDB_NO_ID && have_album)
        artwork_id = album.artwork_id;
    helikon_screen_fill_rect(target, 16u, 38u, 96u, 96u, MUSIC_PAPER);
    helikon_screen_fill_rect(target, 16u, 38u, 96u, 1u, MUSIC_INK);
    helikon_screen_fill_rect(target, 16u, 133u, 96u, 1u, MUSIC_INK);
    helikon_screen_fill_rect(target, 16u, 38u, 1u, 96u, MUSIC_INK);
    helikon_screen_fill_rect(target, 111u, 38u, 1u, 96u, MUSIC_INK);
    (void)draw_album_art(target, artwork_id, 17u, 39u, 94u, flags);

    (void)helikon_text_draw_wrapped(target, 124, 56, MUSIC_FONT_17, MUSIC_INK,
                                    title, 180u, 2u, 20u);
    helikon_text_draw(target, 124, 98, MUSIC_FONT_12, MUSIC_INK, artist, 180u);
    helikon_text_draw(target, 124, 114, MUSIC_FONT_12, MUSIC_MUTED, album_name,
                      180u);
    char track_line[96];
    format_track_line(track_line, sizeof(track_line), track_id,
                      data->duration_ms);
    helikon_text_draw(target, 124, 130, MUSIC_FONT_11, MUSIC_MUTED, track_line,
                      180u);

    const char *state_label;
    int state_x = 124;
    switch (data->state) {
    case NOW_PLAYING_PLAYING:
        state_label = "PLAYING";
        draw_play_triangle(target, 124, 136, 8, 11, MUSIC_INK);
        state_x = 136;
        break;
    case NOW_PLAYING_PAUSED:
        state_label = "PAUSED";
        draw_pause_icon(target, 124, 146, MUSIC_INK);
        state_x = 138;
        break;
    case NOW_PLAYING_OPENING:
        state_label = "OPENING";
        break;
    case NOW_PLAYING_FINISHED:
        state_label = "FINISHED";
        break;
    default:
        state_label = "PLAYING";
        break;
    }
    helikon_text_draw(target, state_x, 146, MUSIC_FONT_14M, MUSIC_INK,
                      state_label, 160u);

    int active = (data->state == NOW_PLAYING_PLAYING ||
                  data->state == NOW_PLAYING_PAUSED) &&
                 data->peaks_active;
    draw_visualizer(target, 16u, 140u, 96u, 16u, data->peaks, active,
                    data->visualizer_mode);

    uint32_t position = data->position_ms;
    uint32_t duration = data->duration_ms;
    if (data->state == NOW_PLAYING_FINISHED && duration != 0u)
        position = duration;
    draw_progress(target, position, duration);
    char elapsed[24];
    char total[24];
    format_time(position, elapsed, sizeof(elapsed));
    if (duration == 0u)
        copy_text(total, sizeof(total), "--:--");
    else
        format_time(duration, total, sizeof(total));
    char time_line[64];
    copy_text(time_line, sizeof(time_line), elapsed);
    append_text(time_line, sizeof(time_line), " / ");
    append_text(time_line, sizeof(time_line), total);
    helikon_text_draw(target, (int32_t)MUSIC_X, 184, MUSIC_FONT_11, MUSIC_INK,
                      time_line, 150u);

    draw_rule(target, 198u, MUSIC_FAINT);
    char source[128];
    char output[128];
    format_source_line(source, sizeof(source), data);
    format_output_line(output, sizeof(output), data);
    helikon_text_draw(target, (int32_t)MUSIC_X, 210, MUSIC_FONT_11,
                      MUSIC_MUTED, source, 288u);
    helikon_text_draw(target, (int32_t)MUSIC_X, 224, MUSIC_FONT_11,
                      MUSIC_MUTED, output, 288u);

    const char *footer;
    if (data->status_override != NULL) {
        footer = data->status_override;
    } else if (data->state == NOW_PLAYING_OPENING) {
        footer = "PLEASE WAIT";
    } else if (data->state == NOW_PLAYING_PAUSED) {
        footer = have_album ? "PLAY RESUME \xc2\xb7 SELECT ALBUM \xc2\xb7 MENU BACK"
                            : "PLAY RESUME \xc2\xb7 MENU BACK";
    } else if (data->state == NOW_PLAYING_FINISHED) {
        footer = have_album ? "PLAY REPLAY \xc2\xb7 SELECT ALBUM \xc2\xb7 MENU BACK"
                            : "PLAY REPLAY \xc2\xb7 MENU BACK";
    } else {
        footer = have_album ? "PLAY PAUSE \xc2\xb7 SELECT ALBUM \xc2\xb7 MENU BACK"
                            : "PLAY PAUSE \xc2\xb7 MENU BACK";
    }
    helikon_text_draw(target, (int32_t)MUSIC_X, 238, MUSIC_FONT_11,
                      MUSIC_MUTED, footer, 288u);
}
#endif

#ifdef HELIKON_AUDIO_EMBEDDED
static void fill_now_playing_source(struct now_playing_data *data,
                                    const struct helikon_player *playback)
{
    if (playback->wav.sample_rate == 0u) {
        data->source_known = 0;
        return;
    }
    data->source_known = 1;
    data->source_rate = playback->wav.sample_rate;
    data->source_channels = playback->wav.channels;
    data->source_bits = playback->wav.bits;
    data->source_bitrate_kbps =
        (playback->wav.sample_rate * playback->wav.channels *
         playback->wav.bits) /
        1000u;
    data->output_rate = playback->audio.sample_rate;
    data->container = "WAV";
    data->codec = "PCM";
}

static int album_exists(uint32_t album_id)
{
    struct hdb_album album;
    return hdb_album(&db, album_id, &album) == HELIKON_OK;
}

static int now_playing_retryable(const struct helikon_player *playback)
{
    switch (playback->last_stage) {
    case HELIKON_AUDIO_STAGE_DMA_SETUP:
    case HELIKON_AUDIO_STAGE_DMA_WAIT:
    case HELIKON_AUDIO_STAGE_DMA_ACTIVE:
    case HELIKON_AUDIO_STAGE_CACHE:
    case HELIKON_AUDIO_STAGE_BUFFER:
    case HELIKON_AUDIO_STAGE_DMA_CLOCK:
    case HELIKON_AUDIO_STAGE_DMA_CONFIG:
    case HELIKON_AUDIO_STAGE_STREAM_UNDERRUN:
        return 1;
    case HELIKON_AUDIO_STAGE_CODEC:
    case HELIKON_AUDIO_STAGE_RATE:
    case HELIKON_AUDIO_STAGE_I2S:
    case HELIKON_AUDIO_STAGE_I2S_FIFO:
    case HELIKON_AUDIO_STAGE_NONE:
    default:
        break;
    }
    return 0;
}

static void make_error_reason(const struct helikon_player *playback,
                              char *buffer, uint32_t capacity)
{
    copy_text(buffer, capacity, "AUDIO ");
    append_text(buffer, capacity, helikon_player_error_text(playback));
}

#endif

#ifdef HELIKON_AUDIO_EMBEDDED
static int wait_now_playing_error(const char *reason, int retryable,
                                  uint32_t stopped_ms, uint32_t track_id,
                                  uint32_t album_id, int have_album)
{
    (void)reason;
    (void)stopped_ms;
    (void)track_id;
    (void)album_id;
    struct nav_state nav;
    nav_reset(&nav);
    uint64_t idle = helikon_clock_now_us(clock);
    for (;;) {
        uint32_t events = 0u;
        int32_t wheel = 0;
        int hold = 0;
        if (helikon_buttons_poll(buttons, &events, &wheel, &hold, 20u) ==
            HELIKON_OK) {
            uint32_t pressed = events & ~nav.previous_events;
            nav.previous_events = events;
            if (hold || (pressed & HELIKON_BUTTON_MENU) != 0u)
                return 0;
            if ((pressed & HELIKON_BUTTON_SELECT) != 0u) {
                if (retryable)
                    return 1;
                return have_album ? 2 : 0;
            }
        }
        if (helikon_clock_expired(clock, idle, MUSIC_IDLE_MS))
            return 0;
        helikon_clock_delay_us(clock, 20000u);
    }
}
#endif

#ifdef HELIKON_AUDIO_EMBEDDED
static void draw_now_playing_error_screen(uint32_t track_id, uint32_t album_id,
                                          const struct helikon_player *playback,
                                          const char *reason, int retryable,
                                          uint32_t stopped_ms)
{
    struct now_playing_data data;
    helikon_memset(&data, 0, sizeof(data));
    data.state = NOW_PLAYING_ERROR;
    data.error_reason = reason;
    data.retryable = retryable;
    data.stopped_ms = stopped_ms;
    fill_now_playing_source(&data, playback);
    draw_now_playing_view(screen, track_id, album_id, &data, 0u);
    (void)helikon_screen_present_all(screen);
}

#endif

static void draw_track_missing_screen(void)
{
    helikon_screen_fill_rect(screen, 0u, 0u, screen->width, screen->height,
                             MUSIC_PAPER);
    draw_top_bar(screen, "NOW PLAYING");
    helikon_text_draw(screen, (int32_t)MUSIC_X, 60, MUSIC_FONT_12, MUSIC_MUTED,
                      "TRACK DATA MISSING", 288u);
    helikon_text_draw(screen, (int32_t)MUSIC_X, 232, MUSIC_FONT_11, MUSIC_MUTED,
                      "MENU BACK", 288u);
    (void)helikon_screen_present_all(screen);
}

static void wait_for_back(void)
{
    struct nav_state nav;
    nav_reset(&nav);
    uint64_t idle = helikon_clock_now_us(clock);
    for (;;) {
        uint32_t events = 0u;
        int32_t wheel = 0;
        int hold = 0;
        if (helikon_buttons_poll(buttons, &events, &wheel, &hold, 20u) ==
            HELIKON_OK) {
            uint32_t pressed = events & ~nav.previous_events;
            nav.previous_events = events;
            if (hold ||
                (pressed & (HELIKON_BUTTON_MENU | HELIKON_BUTTON_SELECT |
                            HELIKON_BUTTON_PLAY)) != 0u)
                return;
        }
        if (helikon_clock_expired(clock, idle, MUSIC_IDLE_MS))
            return;
        helikon_clock_delay_us(clock, 20000u);
    }
}

#ifdef HELIKON_AUDIO_EMBEDDED
static int run_now_playing(uint32_t track_id, uint32_t album_id)
{
    struct now_playing_data data;
    char reason[64];
    uint8_t peaks[HELIKON_AUDIO_PEAK_BARS];
    uint64_t last_draw;
    uint64_t last_visualizer;
    int paused;
    int finished;
    uint64_t underrun_start;
    int result;
    int show_opening = 1;

    for (;;) {
        if (show_opening) {
            helikon_memset(&data, 0, sizeof(data));
            data.state = NOW_PLAYING_OPENING;
            draw_now_playing_view(screen, track_id, album_id, &data, 0u);
            (void)helikon_screen_present_all(screen);
        }

        helikon_memset(&player, 0, sizeof(player));
        size_t audio_size = (size_t)(helikon_audio_end - helikon_audio_start);
        int rc = helikon_player_open(&player, helikon_audio_start, audio_size,
                                     buttons->pmu, clock, 2000u);
        if (rc != HELIKON_OK) {
            make_error_reason(&player, reason, sizeof(reason));
            int retryable = now_playing_retryable(&player);
            draw_now_playing_error_screen(track_id, album_id, &player, reason,
                                          retryable, 0u);
            result = wait_now_playing_error(reason, retryable, 0u, track_id,
                                            album_id,
                                            album_exists(album_id));
            helikon_player_stop(&player);
            if (result == 1) {
                show_opening = 0;
                continue;
            }
            return result == 2 ? 1 : 0;
        }

        paused = 0;
        finished = 0;
        underrun_start = 0u;
        last_draw = helikon_clock_now_us(clock);
        struct nav_state nav;
        nav_reset(&nav);
        int start_rc = helikon_player_play_chunk(&player, 2047u);
        if (start_rc < 0) {
            make_error_reason(&player, reason, sizeof(reason));
            int retryable = now_playing_retryable(&player);
            draw_now_playing_error_screen(track_id, album_id, &player, reason,
                                          retryable, 0u);
            result = wait_now_playing_error(reason, retryable, 0u, track_id,
                                            album_id,
                                            album_exists(album_id));
            helikon_player_stop(&player);
            if (result == 1) {
                show_opening = 0;
                continue;
            }
            return result == 2 ? 1 : 0;
        }

        helikon_memset(&data, 0, sizeof(data));
        data.state = NOW_PLAYING_PLAYING;
        data.position_ms = helikon_player_position_ms(&player);
        data.duration_ms = helikon_player_duration_ms(&player);
        data.peaks_active = 1;
        data.visualizer_mode = visualizer_mode;
        fill_now_playing_source(&data, &player);
        helikon_audio_peak_levels(&player.audio, peaks,
                                  HELIKON_AUDIO_PEAK_BARS);
        data.peaks = peaks;
        draw_now_playing_view(screen, track_id, album_id, &data, 0u);
        (void)helikon_screen_present_all(screen);
        last_draw = helikon_clock_now_us(clock);
        last_visualizer = last_draw;

        for (;;) {
            int chunk_rc = helikon_player_play_chunk(&player, 2047u);
            uint64_t now = helikon_clock_now_us(clock);
            if (chunk_rc < 0) {
                if (player.last_stage == HELIKON_AUDIO_STAGE_STREAM_UNDERRUN &&
                    (underrun_start == 0u ||
                     now - underrun_start < 2000000u)) {
                    if (underrun_start == 0u)
                        underrun_start = now;
                } else {
                    make_error_reason(&player, reason, sizeof(reason));
                    int retryable = now_playing_retryable(&player);
                    draw_now_playing_error_screen(track_id, album_id, &player,
                                                  reason, retryable,
                                                  helikon_player_position_ms(
                                                      &player));
                    result = wait_now_playing_error(
                        reason, retryable,
                        helikon_player_position_ms(&player), track_id, album_id,
                        album_exists(album_id));
                    helikon_player_stop(&player);
                    if (result == 1) {
                        show_opening = 0;
                        goto reopen;
                    }
                    return result == 2 ? 1 : 0;
                }
            } else if (underrun_start != 0u) {
                underrun_start = 0u;
            }

            if (helikon_player_finished(&player)) {
                finished = 1;
                break;
            }

            uint32_t visualizer_interval =
                visualizer_mode == 0u
                    ? 100000u
                    : (visualizer_mode == 1u ? 200000u : 0u);
            if (!paused && visualizer_interval != 0u &&
                now - last_visualizer >= visualizer_interval) {
                uint8_t live[HELIKON_AUDIO_PEAK_BARS];
                helikon_audio_peak_levels(&player.audio, live,
                                          HELIKON_AUDIO_PEAK_BARS);
                helikon_screen_fill_rect(screen, 16u, 140u, 96u, 17u,
                                         MUSIC_PAPER);
                draw_visualizer(screen, 16u, 140u, 96u, 16u, live, 1,
                                visualizer_mode);
                (void)helikon_screen_present(screen, 16u, 140u, 96u, 17u);
                last_visualizer = now;
            }

            uint32_t events = 0u;
            int32_t wheel = 0;
            int hold = 0;
            if (helikon_buttons_poll(buttons, &events, &wheel, &hold, 10u) ==
                HELIKON_OK) {
                uint32_t pressed = events & ~nav.previous_events;
                nav.previous_events = events;
                if (hold || (pressed & HELIKON_BUTTON_MENU) != 0u) {
                    helikon_player_stop(&player);
                    return 0;
                }
                if ((pressed & HELIKON_BUTTON_SELECT) != 0u) {
                    helikon_player_stop(&player);
                    return 1;
                }
                if ((pressed & HELIKON_BUTTON_PLAY) != 0u) {
                    if (paused) {
                        (void)helikon_player_resume(&player);
                        paused = 0;
                        last_visualizer = 0u;
                    } else {
                        (void)helikon_player_pause(&player);
                        paused = 1;
                    }
                    helikon_memset(&data, 0, sizeof(data));
                    data.state = paused ? NOW_PLAYING_PAUSED
                                        : NOW_PLAYING_PLAYING;
                    data.position_ms = helikon_player_position_ms(&player);
                    data.duration_ms = helikon_player_duration_ms(&player);
                    data.peaks_active = 1;
                    data.visualizer_mode = visualizer_mode;
                    fill_now_playing_source(&data, &player);
                    helikon_audio_peak_levels(&player.audio, peaks,
                                              HELIKON_AUDIO_PEAK_BARS);
                    data.peaks = peaks;
                    if (underrun_start != 0u)
                        data.status_override = "AUDIO UNDERRUN";
                    draw_now_playing_view(screen, track_id, album_id, &data,
                                          0u);
                    (void)helikon_screen_present_all(screen);
                    last_draw = now;
                    last_visualizer = now;
                    continue;
                }
            }

            if (now - last_draw >= 1000000u) {
                helikon_memset(&data, 0, sizeof(data));
                data.state = paused ? NOW_PLAYING_PAUSED
                                    : NOW_PLAYING_PLAYING;
                data.position_ms = helikon_player_position_ms(&player);
                data.duration_ms = helikon_player_duration_ms(&player);
                data.peaks_active = 1;
                data.visualizer_mode = visualizer_mode;
                fill_now_playing_source(&data, &player);
                helikon_audio_peak_levels(&player.audio, peaks,
                                          HELIKON_AUDIO_PEAK_BARS);
                data.peaks = peaks;
                if (underrun_start != 0u)
                    data.status_override = "AUDIO UNDERRUN";
                draw_now_playing_view(screen, track_id, album_id, &data, 0u);
                (void)helikon_screen_present_all(screen);
                last_draw = now;
                last_visualizer = now;
            }
            if (paused)
                helikon_clock_delay_us(clock, 20000u);
        }

        if (finished) {
            helikon_memset(&data, 0, sizeof(data));
            data.state = NOW_PLAYING_FINISHED;
            data.position_ms = helikon_player_duration_ms(&player);
            data.duration_ms = helikon_player_duration_ms(&player);
            data.peaks_active = 0;
            data.visualizer_mode = visualizer_mode;
            fill_now_playing_source(&data, &player);
            draw_now_playing_view(screen, track_id, album_id, &data, 0u);
            (void)helikon_screen_present_all(screen);
        }

reopen:
        helikon_player_stop(&player);
        if (finished) {
            nav_reset(&nav);
            uint64_t idle = helikon_clock_now_us(clock);
            for (;;) {
                uint32_t events = 0u;
                int32_t wheel = 0;
                int hold = 0;
                if (helikon_buttons_poll(buttons, &events, &wheel, &hold,
                                         20u) == HELIKON_OK) {
                    uint32_t pressed = events & ~nav.previous_events;
                    nav.previous_events = events;
                    if (hold || (pressed & HELIKON_BUTTON_MENU) != 0u)
                        return 0;
                    if ((pressed & HELIKON_BUTTON_SELECT) != 0u)
                        return 1;
                    if ((pressed & HELIKON_BUTTON_PLAY) != 0u) {
                        show_opening = 1;
                        goto replay;
                    }
                }
                if (helikon_clock_expired(clock, idle, MUSIC_IDLE_MS))
                    return 0;
                helikon_clock_delay_us(clock, 20000u);
            }
        }
replay:
        continue;
    }
}

#else
static int run_now_playing(uint32_t track_id, uint32_t album_id)
{
    (void)track_id;
    (void)album_id;
    return 0;
}
#endif

static void run_album(uint32_t album_id);

static void run_track_page(uint32_t track_id)
{
    struct hdb_track track;
    if (hdb_track(&db, track_id, &track) != HELIKON_OK) {
        draw_track_missing_screen();
        wait_for_back();
        return;
    }
    if (!track_has_audio(track_id)) {
        draw_track_missing_screen();
        wait_for_back();
        return;
    }
    int result = run_now_playing(track_id, track.album_id);
    if (result == 1 && track.album_id < db.album_count) {
        run_album(track.album_id);
    }
}

static void run_album(uint32_t album_id)
{
    struct hdb_album album;
    if (hdb_album(&db, album_id, &album) != HELIKON_OK) {
        helikon_screen_fill_rect(screen, 0u, 0u, screen->width, screen->height,
                                 MUSIC_PAPER);
        draw_top_bar(screen, "ALBUM");
        helikon_text_draw(screen, (int32_t)MUSIC_X, 60, MUSIC_FONT_12,
                          MUSIC_MUTED, "MISSING ALBUM", 288u);
        (void)helikon_screen_present_all(screen);
        wait_for_back();
        return;
    }
    int32_t focus = first_playable_album_track(&album, 0u);
    uint32_t track_top = 0u;
    draw_album_page(screen, album_id, focus, track_top, 0u);
    struct nav_state nav;
    nav_reset(&nav);
    uint64_t idle = helikon_clock_now_us(clock);
    for (;;) {
        uint32_t events = 0u;
        int32_t wheel = 0;
        int hold = 0;
        if (helikon_buttons_poll(buttons, &events, &wheel, &hold, 20u) ==
            HELIKON_OK) {
            uint64_t now = helikon_clock_now_us(clock);
            uint32_t pressed = events & ~nav.previous_events;
            nav.previous_events = events;
            if (hold || (pressed & HELIKON_BUTTON_MENU) != 0u)
                return;
            if ((pressed & (HELIKON_BUTTON_SELECT | HELIKON_BUTTON_PLAY)) !=
                0u) {
                uint32_t playable = album_playable_count(&album, 0u);
                if (playable != 0u) {
                    int32_t start = focus;
                    if (start < 0)
                        start = first_playable_album_track(&album, 0u);
                    if (start >= 0 &&
                        album_track_playable(&album, (uint32_t)start, 0u)) {
                        (void)run_now_playing(
                            album.first_track + (uint32_t)start, album_id);
                        nav_reset(&nav);
                        draw_album_page(screen, album_id, focus, track_top,
                                        0u);
                        idle = helikon_clock_now_us(clock);
                    }
                }
                continue;
            }
            int delta = nav_delta(&nav, pressed, wheel, now);
            if (delta != 0) {
                focus = next_playable_album_track(&album, focus, delta, 0u);
                if (focus >= 0) {
                    if ((uint32_t)focus < track_top)
                        track_top = (uint32_t)focus;
                    else if ((uint32_t)focus >= track_top + 3u)
                        track_top = (uint32_t)focus - 2u;
                }
                idle = now;
                draw_album_page(screen, album_id, focus, track_top, 0u);
            }
        }
        if (helikon_clock_expired(clock, idle, MUSIC_IDLE_MS))
            return;
        helikon_clock_delay_us(clock, 20000u);
    }
}

static void run_artist(uint32_t artist_id)
{
    struct hdb_artist artist;
    if (hdb_artist(&db, artist_id, &artist) != HELIKON_OK) {
        draw_artist_page(screen, artist_id, 0u, 0u, 0u, 0u);
        (void)helikon_screen_present_all(screen);
        wait_for_back();
        return;
    }

    uint32_t tab = 0u;
    uint32_t album_selected = 0u;
    int32_t track_selected = artist_first_playable(artist_id);
    if (track_selected < 0)
        track_selected = 0;
    draw_artist_page(screen, artist_id, tab, album_selected,
                     (uint32_t)track_selected, 0u);
    struct nav_state nav;
    nav_reset(&nav);
    uint64_t idle = helikon_clock_now_us(clock);
    for (;;) {
        uint32_t events = 0u;
        int32_t wheel = 0;
        int hold = 0;
        if (helikon_buttons_poll(buttons, &events, &wheel, &hold, 20u) ==
            HELIKON_OK) {
            uint64_t now = helikon_clock_now_us(clock);
            uint32_t pressed = events & ~nav.previous_events;
            nav.previous_events = events;
            if (hold || (pressed & HELIKON_BUTTON_MENU) != 0u)
                return;

            if ((pressed & HELIKON_BUTTON_LEFT) != 0u) {
                tab = tab == 0u ? 2u : tab - 1u;
                nav.wheel_accumulator = 0;
                idle = now;
                draw_artist_page(screen, artist_id, tab, album_selected,
                                 (uint32_t)track_selected, 0u);
                continue;
            }
            if ((pressed & HELIKON_BUTTON_RIGHT) != 0u) {
                tab = tab == 2u ? 0u : tab + 1u;
                nav.wheel_accumulator = 0;
                idle = now;
                draw_artist_page(screen, artist_id, tab, album_selected,
                                 (uint32_t)track_selected, 0u);
                continue;
            }
            if ((pressed & HELIKON_BUTTON_SELECT) != 0u) {
                if (tab == 0u && artist.album_count != 0u) {
                    uint32_t album_id = 0u;
                    if (artist_album_at(artist_id, album_selected, &album_id) ==
                        HELIKON_OK) {
                        run_album(album_id);
                        nav_reset(&nav);
                        draw_artist_page(screen, artist_id, tab,
                                         album_selected,
                                         (uint32_t)track_selected, 0u);
                        idle = helikon_clock_now_us(clock);
                    }
                } else if (tab == 2u) {
                    uint32_t track_id = 0u;
                    if (artist_track_at(artist_id, (uint32_t)track_selected,
                                        &track_id) == HELIKON_OK &&
                        track_has_audio(track_id)) {
                        run_track_page(track_id);
                        nav_reset(&nav);
                        draw_artist_page(screen, artist_id, tab,
                                         album_selected,
                                         (uint32_t)track_selected, 0u);
                        idle = helikon_clock_now_us(clock);
                    }
                }
                continue;
            }
            int delta = nav_delta(
                &nav,
                pressed & ~(HELIKON_BUTTON_LEFT | HELIKON_BUTTON_RIGHT), wheel,
                now);
            if (delta != 0) {
                if (tab == 0u && artist.album_count != 0u) {
                    if (delta < 0 && album_selected > 0u)
                        --album_selected;
                    else if (delta > 0 &&
                             album_selected + 1u < artist.album_count)
                        ++album_selected;
                } else if (tab == 2u) {
                    int32_t next = artist_next_playable(artist_id,
                                                        track_selected, delta);
                    if (next >= 0)
                        track_selected = next;
                }
                idle = now;
                draw_artist_page(screen, artist_id, tab, album_selected,
                                 (uint32_t)track_selected, 0u);
            }
        }
        if (helikon_clock_expired(clock, idle, MUSIC_IDLE_MS))
            return;
        helikon_clock_delay_us(clock, 20000u);
    }
}

static int run_wall(enum wall_kind kind, uint32_t *selected_io)
{
    uint32_t count = wall_count(kind);
    uint32_t selected = selected_io != NULL ? *selected_io : 0u;
    if (count != 0u && selected >= count)
        selected = count - 1u;
    if (selected_io != NULL)
        *selected_io = selected;

    if (count == 0u) {
        draw_wall(screen, kind, 0u);
        struct nav_state nav;
        nav_reset(&nav);
        uint64_t idle = helikon_clock_now_us(clock);
        for (;;) {
            uint32_t events = 0u;
            int32_t wheel = 0;
            int hold = 0;
            if (helikon_buttons_poll(buttons, &events, &wheel, &hold, 20u) ==
                HELIKON_OK) {
                uint32_t pressed = events & ~nav.previous_events;
                nav.previous_events = events;
                if (hold || (pressed & HELIKON_BUTTON_ANY) != 0u)
                    return -1;
            }
            if (helikon_clock_expired(clock, idle, MUSIC_IDLE_MS))
                return -1;
            helikon_clock_delay_us(clock, 20000u);
        }
    }

    struct nav_state nav;
    nav_reset(&nav);
    uint64_t idle = helikon_clock_now_us(clock);
    draw_wall(screen, kind, selected);

    for (;;) {
        uint32_t events = 0u;
        int32_t wheel = 0;
        int hold = 0;
        if (helikon_buttons_poll(buttons, &events, &wheel, &hold, 20u) ==
            HELIKON_OK) {
            uint64_t now = helikon_clock_now_us(clock);
            uint32_t pressed = events & ~nav.previous_events;
            nav.previous_events = events;

            if (hold)
                return -1;
            if ((pressed & HELIKON_BUTTON_MENU) != 0u)
                return -1;
            if ((pressed & (HELIKON_BUTTON_SELECT | HELIKON_BUTTON_PLAY)) !=
                0u) {
                struct wall_item item;
                if (wall_item(kind, selected, &item) == HELIKON_OK) {
                    if (selected_io != NULL)
                        *selected_io = selected;
                    return (int)item.id;
                }
                return -1;
            }
            int delta = nav_delta(&nav, pressed, wheel, now);
            int changed = 0;
            if (delta < 0 && selected > 0u) {
                --selected;
                changed = 1;
            } else if (delta > 0 && selected + 1u < count) {
                ++selected;
                changed = 1;
            }
            if (changed) {
                if (selected_io != NULL)
                    *selected_io = selected;
                idle = now;
                draw_wall(screen, kind, selected);
            }
        }
        if (helikon_clock_expired(clock, idle, MUSIC_IDLE_MS))
            return -1;
        helikon_clock_delay_us(clock, 20000u);
    }
}

static void run_albums(void)
{
    uint32_t selected = 0u;
    for (;;) {
        int album_id = run_wall(WALL_ALBUMS, &selected);
        if (album_id < 0)
            return;
        run_album((uint32_t)album_id);
    }
}

static void run_artists(void)
{
    uint32_t count = db.artist_count;
    uint32_t selected = 0u;
    uint32_t top = 0u;
    draw_artists_list_screen(screen, selected, top, 0u);
    struct nav_state nav;
    nav_reset(&nav);
    uint64_t idle = helikon_clock_now_us(clock);
    for (;;) {
        uint32_t events = 0u;
        int32_t wheel = 0;
        int hold = 0;
        if (helikon_buttons_poll(buttons, &events, &wheel, &hold, 20u) ==
            HELIKON_OK) {
            uint64_t now = helikon_clock_now_us(clock);
            uint32_t pressed = events & ~nav.previous_events;
            nav.previous_events = events;
            if (hold || (pressed & HELIKON_BUTTON_MENU) != 0u)
                return;
            if ((pressed & HELIKON_BUTTON_SELECT) != 0u && count != 0u) {
                run_artist(selected);
                nav_reset(&nav);
                draw_artists_list_screen(screen, selected, top, 0u);
                idle = helikon_clock_now_us(clock);
                continue;
            }
            int delta = nav_delta(&nav, pressed, wheel, now);
            if (delta != 0) {
                if (delta < 0 && selected > 0u)
                    --selected;
                else if (delta > 0 && selected + 1u < count)
                    ++selected;
                if (selected < top)
                    top = selected;
                else if (selected >= top + 4u)
                    top = selected - 3u;
                idle = now;
                draw_artists_list_screen(screen, selected, top, 0u);
            }
        }
        if (helikon_clock_expired(clock, idle, MUSIC_IDLE_MS))
            return;
        helikon_clock_delay_us(clock, 20000u);
    }
}

static void run_tracks(void)
{
    uint32_t selected = 0u;
    for (;;) {
        struct list_spec spec = { .kind = LIST_SONGS, .parent = 0u };
        int track_id = run_list(&spec, &selected);
        if (track_id < 0)
            return;
        run_track_page((uint32_t)track_id);
    }
}

/* Returns 0 for Albums, 1 for Artists, 2 for Tracks, or -1 on MENU/timeout. */
static int run_hub(uint32_t *selected_io)
{
    uint32_t selected = selected_io != NULL ? *selected_io : 0u;
    if (selected >= HELIKON_MUSIC_HUB_CELLS)
        selected = 0u;

    struct nav_state nav;
    nav_reset(&nav);
    uint64_t idle = helikon_clock_now_us(clock);
    draw_hub(screen, selected);

    for (;;) {
        uint32_t events = 0u;
        int32_t wheel = 0;
        int hold = 0;
        if (helikon_buttons_poll(buttons, &events, &wheel, &hold, 20u) ==
            HELIKON_OK) {
            uint64_t now = helikon_clock_now_us(clock);
            uint32_t pressed = events & ~nav.previous_events;
            nav.previous_events = events;

            if (hold)
                return -1;
            if ((pressed & HELIKON_BUTTON_MENU) != 0u)
                return -1;
            if ((pressed & (HELIKON_BUTTON_SELECT | HELIKON_BUTTON_PLAY)) !=
                0u) {
                if (selected_io != NULL)
                    *selected_io = selected;
                return (int)selected;
            }
            int delta = nav_delta(&nav, pressed, wheel, now);
            if (delta < 0) {
                selected = selected == 0u ? HELIKON_MUSIC_HUB_CELLS - 1u
                                          : selected - 1u;
            } else if (delta > 0) {
                selected = selected + 1u == HELIKON_MUSIC_HUB_CELLS
                               ? 0u
                               : selected + 1u;
            }
            if (delta != 0) {
                if (selected_io != NULL)
                    *selected_io = selected;
                idle = now;
                draw_hub(screen, selected);
            }
        }
        if (helikon_clock_expired(clock, idle, MUSIC_IDLE_MS))
            return -1;
        helikon_clock_delay_us(clock, 20000u);
    }
}

int helikon_music_run(struct helikon_screen *screen_arg,
                      struct helikon_buttons *buttons_arg,
                      struct helikon_clock *clock_arg)
{
    screen = screen_arg;
    buttons = buttons_arg;
    clock = clock_arg;
    if (screen == NULL || buttons == NULL || clock == NULL)
        return HELIKON_ERR_ARGUMENT;
    helikon_text_init();

    const struct hdb_io io = { .read = blob_read, .context = NULL };
    if (hdb_open(&db, &io) != HELIKON_OK) {
        draw_frame(screen, "MUSIC");
        helikon_screen_set_foreground(screen, MUSIC_INK);
        helikon_screen_text(screen, 14u, 60u, 2u, "LIBRARY UNAVAILABLE");
        helikon_screen_text(screen, 14u, 90u, 1u,
                            "REBUILD THE IMAGE WITH A VALID HDB");
        (void)helikon_screen_present_all(screen);
        for (;;) {
            uint32_t events = 0u;
            int32_t wheel = 0;
            int hold = 0;
            if (helikon_buttons_poll(buttons, &events, &wheel, &hold, 20u) ==
                    HELIKON_OK &&
                ((events & HELIKON_BUTTON_ANY) != 0u || hold))
                return HELIKON_OK;
            helikon_clock_delay_us(clock, 20000u);
        }
    }

    uint32_t selected = 0u;
    for (;;) {
        int choice = run_hub(&selected);
        if (choice < 0)
            return HELIKON_OK;
        if (choice == 0) {
            run_albums();
        } else if (choice == 1) {
            run_artists();
        } else {
            run_tracks();
        }
    }
}

#else

int helikon_music_run(struct helikon_screen *screen,
                      struct helikon_buttons *buttons,
                      struct helikon_clock *clock)
{
    (void)screen;
    (void)buttons;
    (void)clock;
    return HELIKON_ERR_UNSUPPORTED;
}

#endif
