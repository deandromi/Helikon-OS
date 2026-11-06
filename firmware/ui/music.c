/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "music.h"

#include <stddef.h>

#include "error.h"

#ifdef HELIKON_HDB_EMBEDDED

#include "hdb.h"
#include "mem.h"

#define MUSIC_PAPER 0xef7cu
#define MUSIC_INK 0x2103u
#define MUSIC_MUTED 0x6b2bu
#define MUSIC_ART_SIZE 96u
#define MUSIC_ROWS 8u
#define MUSIC_ROW_TOP 42u
#define MUSIC_ROW_HEIGHT 21u
#define MUSIC_IDLE_MS 60000u

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
    LIST_ROOT,
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
    case LIST_ROOT:
        return 3u;
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
    case LIST_ROOT: {
        static const char *const root_items[3] = { "ARTISTS", "ALBUMS",
                                                   "SONGS" };
        if (index >= 3u)
            return HELIKON_ERR_NOT_FOUND;
        copy_text(label, capacity, root_items[index]);
        *id = index;
        return HELIKON_OK;
    }
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
    case LIST_ROOT:
        copy_text(title, capacity, "MUSIC");
        break;
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
        copy_text(title, capacity, "SONGS (");
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
    char title[192];
    list_title(spec, count, title, sizeof(title));
    draw_frame(target, title);

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
static int run_list(const struct list_spec *spec)
{
    uint32_t count = list_count(spec);
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

    uint32_t selected = 0u;
    uint32_t top = 0u;
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
        draw_frame(screen, "ARTIST");
        helikon_screen_text(screen, 14u, 60u, 2u, "MISSING ARTIST");
        (void)helikon_screen_present_all(screen);
        return;
    }

    draw_frame(screen, "ARTIST");
    int has_art = load_art(artist.image_id);
    uint32_t x = 14u;
    if (has_art) {
        helikon_screen_bitmap(screen, 12u, 38u, MUSIC_ART_SIZE,
                              MUSIC_ART_SIZE, art_pixels, MUSIC_ART_SIZE);
        x = 124u;
    }
    char name[128];
    read_str(artist.name_off, name, sizeof(name));
    draw_text(screen, x, 40u, 2u, name, has_art ? 24u : 46u);

    char bio[192];
    read_str(artist.bio_off, bio, sizeof(bio));
    if (bio[0] != '\0') {
        draw_wrapped(screen, x, 64u, 1u, bio, has_art ? 42u : 70u, 6u);
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
        draw_frame(screen, "ALBUM");
        helikon_screen_text(screen, 14u, 60u, 2u, "MISSING ALBUM");
        (void)helikon_screen_present_all(screen);
        return;
    }

    draw_frame(screen, "ALBUM");
    int has_art = load_art(album.artwork_id);
    uint32_t x = 14u;
    if (has_art) {
        helikon_screen_bitmap(screen, 12u, 38u, MUSIC_ART_SIZE,
                              MUSIC_ART_SIZE, art_pixels, MUSIC_ART_SIZE);
        x = 124u;
    }
    char title[128];
    read_str(album.title_off, title, sizeof(title));
    draw_wrapped(screen, x, 40u, 2u, title, has_art ? 24u : 44u, 3u);

    char info[64];
    copy_text(info, sizeof(info), "");
    append_u32(info, sizeof(info), album.year);
    append_text(info, sizeof(info), "   RATING ");
    append_u32(info, sizeof(info), album.rating);
    helikon_screen_text(screen, x, 88u, 2u, info);
    copy_text(info, sizeof(info), "");
    append_u32(info, sizeof(info), album.track_count);
    append_text(info, sizeof(info), " TRACKS");
    helikon_screen_text(screen, x, 108u, 2u, info);
    draw_footer_select(screen, "SELECT: TRACKS");
    (void)helikon_screen_present_all(screen);
}

static void draw_track_page(uint32_t track_id)
{
    struct hdb_track track;
    if (hdb_track(&db, track_id, &track) != HELIKON_OK) {
        draw_frame(screen, "TRACK");
        helikon_screen_text(screen, 14u, 60u, 2u, "MISSING TRACK");
        (void)helikon_screen_present_all(screen);
        return;
    }

    draw_frame(screen, "TRACK");
    char title[192];
    read_str(track.title_off, title, sizeof(title));
    draw_wrapped(screen, 14u, 42u, 2u, title, 44u, 3u);

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

static void run_album(uint32_t album_id);

static void run_artist(uint32_t artist_id)
{
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
                int album_id = run_list(&spec);
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
                int track_id = run_list(&spec);
                nav_reset(&nav);
                draw_album_page(album_id);
                idle = helikon_clock_now_us(clock);
                if (track_id >= 0) {
                    draw_track_page((uint32_t)track_id);
                    idle = helikon_clock_now_us(clock);
                    struct nav_state track_nav;
                    nav_reset(&track_nav);
                    for (;;) {
                        uint32_t tevents = 0u;
                        int32_t twheel = 0;
                        int thold = 0;
                        if (helikon_buttons_poll(buttons, &tevents, &twheel,
                                                 &thold, 20u) == HELIKON_OK) {
                            uint32_t tpressed =
                                tevents & ~track_nav.previous_events;
                            track_nav.previous_events = tevents;
                            if (thold ||
                                (tpressed & (HELIKON_BUTTON_MENU |
                                             HELIKON_BUTTON_SELECT |
                                             HELIKON_BUTTON_PLAY)) != 0u)
                                break;
                        }
                        if (helikon_clock_expired(clock, idle, MUSIC_IDLE_MS))
                            break;
                        helikon_clock_delay_us(clock, 20000u);
                    }
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

static void run_all_albums(void)
{
    struct list_spec spec = { .kind = LIST_ALBUMS, .parent = 0u };
    int album_id = run_list(&spec);
    if (album_id >= 0)
        run_album((uint32_t)album_id);
}

static void run_all_songs(void)
{
    struct list_spec spec = { .kind = LIST_SONGS, .parent = 0u };
    int track_id = run_list(&spec);
    if (track_id >= 0) {
        draw_track_page((uint32_t)track_id);
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

    for (;;) {
        struct list_spec root = { .kind = LIST_ROOT, .parent = 0u };
        int choice = run_list(&root);
        if (choice < 0)
            return HELIKON_OK;
        if (choice == 0) {
            struct list_spec spec = { .kind = LIST_ARTISTS, .parent = 0u };
            int artist_id = run_list(&spec);
            if (artist_id >= 0)
                run_artist((uint32_t)artist_id);
        } else if (choice == 1) {
            run_all_albums();
        } else {
            run_all_songs();
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
