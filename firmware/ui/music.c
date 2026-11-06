/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "music.h"

#include <stddef.h>

#include "error.h"

#ifdef HELIKON_HDB_EMBEDDED

#include "hdb.h"
#include "launcher_assets.h"
#include "mem.h"

#define MUSIC_PAPER 0xef7cu
#define MUSIC_PAPER_LIGHT 0xf79du
#define MUSIC_INK 0x2103u
#define MUSIC_MUTED 0x6b2bu
#define MUSIC_RULE 0xb573u
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
    LIST_ALBUM_TRACKS
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

static void draw_wrapped(struct helikon_screen *target, uint32_t x, uint32_t y,
                         uint32_t scale, const char *source,
                         uint32_t max_chars, uint32_t max_lines)
{
    char flat[192];
    ascii_text(flat, sizeof(flat), source, max_chars * max_lines);
    uint32_t length = 0u;
    while (flat[length] != '\0')
        ++length;

    uint32_t line = 0u;
    uint32_t start = 0u;
    while (start < length && line < max_lines) {
        uint32_t end = start;
        uint32_t last_space = start;
        while (end < length && end - start < max_chars) {
            if (flat[end] == ' ')
                last_space = end;
            ++end;
        }
        if (end < length && last_space > start)
            end = last_space;
        char saved = flat[end];
        flat[end] = '\0';
        helikon_screen_text(target, x,
                            y + line * (5u * scale + 3u), scale,
                            flat + start);
        flat[end] = saved;
        if (end == start)
            break;
        start = end;
        while (start < length && flat[start] == ' ')
            ++start;
        ++line;
    }
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
    if (hdb_string(&db, offset, buffer, capacity, NULL) != HELIKON_OK) {
        if (capacity != 0u)
            buffer[0] = '\0';
        return HELIKON_ERR_IO;
    }
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

static void blit_header(struct helikon_screen *target, uint32_t header)
{
    const struct helikon_launcher_bitmap *strip =
        &helikon_music_headers[header];
    helikon_screen_bitmap(target, (uint32_t)strip->x, (uint32_t)strip->y,
                          strip->width, strip->height, strip->pixels,
                          strip->width);
}

static void draw_detail_header(struct helikon_screen *target, uint32_t header)
{
    helikon_screen_fill_rect(target, 0u, 0u, target->width, target->height,
                             MUSIC_PAPER);
    blit_header(target, header);
    helikon_screen_set_foreground(target, MUSIC_MUTED);
    helikon_screen_text(target, 14u, 222u, 1u, "MENU: BACK");
    helikon_screen_set_foreground(target, MUSIC_INK);
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

static void draw_footer_select(struct helikon_screen *target, const char *text)
{
    helikon_screen_set_foreground(target, MUSIC_MUTED);
    helikon_screen_text(target, 140u, 222u, 1u, text);
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
    case LIST_ALBUM_TRACKS: {
        struct hdb_album album;
        if (hdb_album(&db, spec->parent, &album) != HELIKON_OK)
            return 0u;
        return album.track_count;
    }
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
    case LIST_ALBUM_TRACKS: {
        struct hdb_album album;
        if (hdb_album(&db, spec->parent, &album) != HELIKON_OK ||
            index >= album.track_count)
            return HELIKON_ERR_NOT_FOUND;
        uint32_t track_id = album.first_track + index;
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
    case LIST_ALBUM_TRACKS: {
        struct hdb_album album;
        if (hdb_album(&db, spec->parent, &album) == HELIKON_OK)
            read_str(album.title_off, title, capacity);
        else
            copy_text(title, capacity, "TRACKS");
        break;
    }
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

static void format_duration(uint32_t duration_ms, char *buffer,
                            uint32_t capacity)
{
    uint32_t seconds = duration_ms / 1000u;
    copy_text(buffer, capacity, "");
    append_u32(buffer, capacity, seconds / 60u);
    append_text(buffer, capacity, ":");
    uint32_t rest = seconds % 60u;
    if (rest < 10u)
        append_text(buffer, capacity, "0");
    append_u32(buffer, capacity, rest);
}

static void draw_artist_page(uint32_t artist_id)
{
    struct hdb_artist artist;
    if (hdb_artist(&db, artist_id, &artist) != HELIKON_OK) {
        draw_detail_header(screen, MUSIC_HEADER_ARTIST);
        helikon_screen_text(screen, 14u, 60u, 2u, "MISSING ARTIST");
        (void)helikon_screen_present_all(screen);
        return;
    }

    draw_detail_header(screen, MUSIC_HEADER_ARTIST);
    int has_art = load_art(artist.image_id);
    uint32_t x = 14u;
    if (has_art) {
        helikon_screen_bitmap(screen, 12u, 44u, MUSIC_ART_SIZE,
                              MUSIC_ART_SIZE, art_pixels, MUSIC_ART_SIZE);
        x = 124u;
    }
    char name[128];
    read_str(artist.name_off, name, sizeof(name));
    draw_text(screen, x, 46u, 2u, name, has_art ? 24u : 46u);

    char bio[192];
    read_str(artist.bio_off, bio, sizeof(bio));
    if (bio[0] != '\0') {
        draw_wrapped(screen, x, 70u, 1u, bio, has_art ? 42u : 70u, 6u);
    }

    char info[64];
    copy_text(info, sizeof(info), "");
    append_u32(info, sizeof(info), artist.album_count);
    append_text(info, sizeof(info), " ALBUMS   ");
    append_u32(info, sizeof(info), artist.track_count);
    append_text(info, sizeof(info), " TRACKS");
    helikon_screen_set_foreground(screen, MUSIC_INK);
    helikon_screen_text(screen, 14u, 186u, 2u, info);
    if (artist.album_count != 0u)
        draw_footer_select(screen, "SELECT: ALBUMS");
    (void)helikon_screen_present_all(screen);
}

static void draw_album_page(uint32_t album_id)
{
    struct hdb_album album;
    if (hdb_album(&db, album_id, &album) != HELIKON_OK) {
        draw_detail_header(screen, MUSIC_HEADER_ALBUM);
        helikon_screen_text(screen, 14u, 60u, 2u, "MISSING ALBUM");
        (void)helikon_screen_present_all(screen);
        return;
    }

    draw_detail_header(screen, MUSIC_HEADER_ALBUM);
    int has_art = load_art(album.artwork_id);
    uint32_t x = 14u;
    if (has_art) {
        helikon_screen_bitmap(screen, 12u, 44u, MUSIC_ART_SIZE,
                              MUSIC_ART_SIZE, art_pixels, MUSIC_ART_SIZE);
        x = 124u;
    }
    char title[128];
    read_str(album.title_off, title, sizeof(title));
    draw_wrapped(screen, x, 46u, 2u, title, has_art ? 24u : 44u, 3u);

    char info[64];
    copy_text(info, sizeof(info), "");
    append_u32(info, sizeof(info), album.year);
    append_text(info, sizeof(info), "   RATING ");
    append_u32(info, sizeof(info), album.rating);
    helikon_screen_text(screen, x, 94u, 2u, info);
    copy_text(info, sizeof(info), "");
    append_u32(info, sizeof(info), album.track_count);
    append_text(info, sizeof(info), " TRACKS");
    helikon_screen_text(screen, x, 114u, 2u, info);
    draw_footer_select(screen, "SELECT: TRACKS");
    (void)helikon_screen_present_all(screen);
}

static void draw_track_page(uint32_t track_id)
{
    struct hdb_track track;
    if (hdb_track(&db, track_id, &track) != HELIKON_OK) {
        draw_detail_header(screen, MUSIC_HEADER_TRACK);
        helikon_screen_text(screen, 14u, 60u, 2u, "MISSING TRACK");
        (void)helikon_screen_present_all(screen);
        return;
    }

    draw_detail_header(screen, MUSIC_HEADER_TRACK);
    char title[192];
    read_str(track.title_off, title, sizeof(title));
    draw_wrapped(screen, 14u, 46u, 2u, title, 44u, 3u);

    struct hdb_artist artist;
    struct hdb_album album;
    char name[128];
    if (hdb_artist(&db, track.artist_id, &artist) == HELIKON_OK) {
        read_str(artist.name_off, name, sizeof(name));
        draw_text(screen, 14u, 106u, 2u, name, 46u);
    }
    if (hdb_album(&db, track.album_id, &album) == HELIKON_OK) {
        read_str(album.title_off, name, sizeof(name));
        draw_text(screen, 14u, 126u, 2u, name, 46u);
    }
    char info[64];
    format_duration(track.duration_ms, info, sizeof(info));
    append_text(info, sizeof(info), "   RATING ");
    append_u32(info, sizeof(info), track.rating);
    helikon_screen_text(screen, 14u, 150u, 2u, info);

    helikon_screen_set_foreground(screen, MUSIC_MUTED);
    helikon_screen_text(screen, 14u, 184u, 2u, "PLAYBACK COMING NEXT");
    helikon_screen_set_foreground(screen, MUSIC_INK);
    (void)helikon_screen_present_all(screen);
}

static void wait_back(void)
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

static void run_track_page(uint32_t track_id)
{
    draw_track_page(track_id);
    wait_back();
}

static void run_album(uint32_t album_id);

static void run_artist(uint32_t artist_id)
{
    uint32_t selected = 0u;
    draw_artist_page(artist_id);
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
            if (hold)
                return;
            if ((pressed & HELIKON_BUTTON_MENU) != 0u)
                return;
            if ((pressed & (HELIKON_BUTTON_SELECT | HELIKON_BUTTON_PLAY)) !=
                0u) {
                struct list_spec spec = { .kind = LIST_ARTIST_ALBUMS,
                                          .parent = artist_id };
                int album_id = run_list(&spec, &selected);
                nav_reset(&nav);
                draw_artist_page(artist_id);
                idle = helikon_clock_now_us(clock);
                if (album_id >= 0) {
                    run_album((uint32_t)album_id);
                    nav_reset(&nav);
                    draw_artist_page(artist_id);
                    idle = helikon_clock_now_us(clock);
                }
            }
        }
        if (helikon_clock_expired(clock, idle, MUSIC_IDLE_MS))
            return;
        helikon_clock_delay_us(clock, 20000u);
    }
}

static void run_album(uint32_t album_id)
{
    uint32_t selected = 0u;
    draw_album_page(album_id);
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
            if (hold)
                return;
            if ((pressed & HELIKON_BUTTON_MENU) != 0u)
                return;
            if ((pressed & (HELIKON_BUTTON_SELECT | HELIKON_BUTTON_PLAY)) !=
                0u) {
                struct list_spec spec = { .kind = LIST_ALBUM_TRACKS,
                                          .parent = album_id };
                int track_id = run_list(&spec, &selected);
                nav_reset(&nav);
                draw_album_page(album_id);
                idle = helikon_clock_now_us(clock);
                if (track_id >= 0) {
                    run_track_page((uint32_t)track_id);
                    nav_reset(&nav);
                    draw_album_page(album_id);
                    idle = helikon_clock_now_us(clock);
                }
            }
        }
        if (helikon_clock_expired(clock, idle, MUSIC_IDLE_MS))
            return;
        helikon_clock_delay_us(clock, 20000u);
    }
}

/* Returns the selected record id, or -1 on MENU/timeout/empty. */
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
    uint32_t selected = 0u;
    for (;;) {
        int artist_id = run_wall(WALL_ARTISTS, &selected);
        if (artist_id < 0)
            return;
        run_artist((uint32_t)artist_id);
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
