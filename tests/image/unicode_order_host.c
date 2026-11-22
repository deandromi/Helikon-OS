/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Differential tool for Apple HFS+ ordering and for reading what macOS wrote.
 *
 * The comparator is a reimplementation of Apple's FastUnicodeCompare, and no
 * unit test we write can prove it matches Apple's own code. What can be checked
 * is the effect: macOS builds a directory's catalog in exactly the order that
 * comparator defines, so reading a real volume back gives an ordering produced
 * by Apple to compare against. `tests/image/run-unicode-order-macos.sh` creates
 * a corpus with macOS and drives this tool.
 *
 *   unicode_order_host list IMAGE [PATH]    names under PATH, catalog order
 *   unicode_order_host tree IMAGE [PATH]    `path<TAB>kind<TAB>size`, recursive
 *   unicode_order_host read IMAGE PATH      file bytes on stdout
 *   unicode_order_host sort                 names on stdin, our order
 *   unicode_order_host canon                names on stdin, canonical form
 *
 * stdout carries the answer and nothing else, one name or record per line, and
 * every name is printed in HFS+ canonical form, so two runs compare with `cmp`.
 * Diagnostics go to stderr: a warning must never become a false pass.
 *
 * Host tooling. It opens the image read-only and never writes it.
 */
#include "hfs_catalog.h"
#include "hfs_catalog_compare.h"
#include "hfs_extents.h"
#include "hfs_file.h"
#include "hfs_unicode.h"
#include "hfs_volume.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ROOT_CNID 2u
#define MAX_DEPTH 5u
#define MAX_NAMES 2048u
#define NAME_BYTES 1024u
#define UNIT_BYTES 512u
#define SCRATCH_BYTES 65536u
#define READ_CHUNK 4096u

struct image {
    int fd;
    struct hfs_volume volume;
    struct hfs_catalog catalog;
    struct hfs_extents extents;
    uint8_t catalog_scratch[SCRATCH_BYTES];
    uint8_t extent_scratch[SCRATCH_BYTES];
};

static int read_at(void *context, uint64_t offset, void *buffer, size_t length)
{
    int fd = *(int *)context;
    ssize_t done = pread(fd, buffer, length, (off_t)offset);
    return done == (ssize_t)length ? 0 : 1;
}

static int open_image(const char *path, struct image *image)
{
    memset(image, 0, sizeof(*image));
    image->fd = open(path, O_RDONLY);
    if (image->fd < 0) {
        perror(path);
        return -1;
    }

    off_t end = lseek(image->fd, 0, SEEK_END);
    if (end < 0) {
        perror("lseek");
        return -1;
    }
    int rc = hfs_volume_probe(&image->volume, read_at, &image->fd, 0,
                              (uint64_t)end);
    if (rc == HFS_ERR_DIRTY) {
        /* Reading is still meaningful; say so rather than hide it. */
        fprintf(stderr, "note: %s has a dirty journal; reading it anyway\n",
                path);
    } else if (rc != HFS_OK) {
        fprintf(stderr, "%s: %s\n", path, hfs_strerror(rc));
        return -1;
    }

    rc = hfs_catalog_open(&image->catalog, &image->volume,
                          image->catalog_scratch, sizeof(image->catalog_scratch));
    if (rc != HFS_OK) {
        fprintf(stderr, "%s: catalog: %s\n", path, hfs_strerror(rc));
        return -1;
    }
    rc = hfs_extents_open(&image->extents, &image->volume,
                          image->extent_scratch, sizeof(image->extent_scratch));
    if (rc != HFS_OK) {
        fprintf(stderr, "%s: extents: %s\n", path, hfs_strerror(rc));
        return -1;
    }
    return 0;
}

/*
 * Walk one directory. `tree` prints the recursive form with a path, sizes and
 * kinds; otherwise it prints the bare names of this directory. Either way the
 * order is the catalog order, which is the order Apple's comparator defines.
 */
static int walk(struct image *image, uint32_t parent, const char *prefix,
                unsigned depth, int tree)
{
    struct hfs_catalog_iterator iterator;
    int rc = hfs_catalog_iter_init(&image->catalog, parent, &iterator);
    if (rc != HFS_OK)
        return rc;

    struct hfs_entry entry;
    for (;;) {
        rc = hfs_catalog_iter_next(&image->catalog, &iterator, &entry);
        if (rc < 0)
            return rc;
        if (rc == 0)
            break;
        if (!tree) {
            printf("%s\n", entry.name);
            continue;
        }

        char path[2u * NAME_BYTES];
        if (*prefix != '\0')
            snprintf(path, sizeof(path), "%s/%s", prefix, entry.name);
        else
            snprintf(path, sizeof(path), "%s", entry.name);

        if (entry.kind == 1u) {
            printf("%s\tfolder\t-\n", path);
            if (depth < MAX_DEPTH) {
                /* `rc` is still the iteration result here: a separate variable
                 * or the recursion reads a plain "one entry" as an error. */
                int sub = walk(image, entry.id, path, depth + 1u, tree);
                if (sub != HFS_OK)
                    return sub;
            }
        } else if (entry.kind == 2u) {
            printf("%s\tfile\t%llu\n", path,
                   (unsigned long long)entry.data.size);
        } else {
            printf("%s\tother\t-\n", path);
        }
    }
    return HFS_OK;
}

/*
 * Resolve `path` to the folder to walk and the prefix to print. "/" is the
 * root; every other path is an exact lookup, so the corpus can live in a
 * subfolder and the root's own hidden entries never enter a comparison.
 */
static int list_tree(struct image *image, const char *path, int tree)
{
    uint32_t parent = ROOT_CNID;
    const char *prefix = "";
    if (strcmp(path, "/") != 0) {
        struct hfs_entry entry;
        int rc = hfs_catalog_lookup(&image->catalog, path, &entry);
        if (rc != HFS_OK) {
            fprintf(stderr, "%s: %s\n", path, hfs_strerror(rc));
            return 1;
        }
        if (entry.kind != 1u) {
            fprintf(stderr, "%s: not a folder\n", path);
            return 1;
        }
        parent = entry.id;
        prefix = path + 1; /* the printed paths stay relative to the volume */
    }

    int rc = walk(image, parent, prefix, 0u, tree);
    if (rc != HFS_OK) {
        fprintf(stderr, "walk: %d (%s)\n", rc, hfs_strerror(rc));
        return 1;
    }
    return fflush(stdout) == 0 ? 0 : 1;
}

static int read_path(struct image *image, const char *path)
{
    struct hfs_entry entry;
    int rc = hfs_catalog_lookup(&image->catalog, path, &entry);
    if (rc != HFS_OK) {
        fprintf(stderr, "%s: %s\n", path, hfs_strerror(rc));
        return 1;
    }
    if (entry.kind != 2u) {
        fprintf(stderr, "%s: not a file\n", path);
        return 1;
    }

    uint8_t buffer[READ_CHUNK];
    uint64_t offset = 0u;
    while (offset < entry.data.size) {
        size_t want = READ_CHUNK;
        if (entry.data.size - offset < want)
            want = (size_t)(entry.data.size - offset);
        size_t done = 0u;
        rc = helikon_hfs_read_file(&image->catalog, &image->extents, path,
                                   offset, buffer, want, &done);
        if (rc != HFS_OK) {
            fprintf(stderr, "%s at %llu: %s\n", path,
                    (unsigned long long)offset, hfs_strerror(rc));
            return 1;
        }
        if (done == 0u) {
            fprintf(stderr, "%s: short read at %llu\n", path,
                    (unsigned long long)offset);
            return 1;
        }
        if (fwrite(buffer, 1u, done, stdout) != done)
            return 1;
        offset += done;
    }
    return fflush(stdout) == 0 ? 0 : 1;
}

/* ------------------------------------------------------------------ names */

struct name {
    char text[NAME_BYTES];
    uint8_t units[UNIT_BYTES];
    size_t unit_count;
    char display[NAME_BYTES];
};

static struct name *names;
static size_t name_count;

/*
 * UTF-8 to UTF-16BE. Rejects overlong forms, surrogate halves and anything past
 * U+10FFFF: a name that does not decode is not a name HFS+ could store.
 */
static int utf8_to_utf16be(const char *text, size_t length, uint8_t *dest,
                           size_t capacity, size_t *units)
{
    size_t used = 0u;
    size_t index = 0u;
    while (index < length) {
        unsigned char lead = (unsigned char)text[index];
        uint32_t code;
        size_t extra;
        if (lead < 0x80u) {
            code = lead;
            extra = 0u;
        } else if ((lead & 0xe0u) == 0xc0u) {
            code = lead & 0x1fu;
            extra = 1u;
        } else if ((lead & 0xf0u) == 0xe0u) {
            code = lead & 0x0fu;
            extra = 2u;
        } else if ((lead & 0xf8u) == 0xf0u) {
            code = lead & 0x07u;
            extra = 3u;
        } else {
            return -1;
        }
        if (extra > length - index - 1u)
            return -1;
        for (size_t k = 1u; k <= extra; ++k) {
            unsigned char next = (unsigned char)text[index + k];
            if ((next & 0xc0u) != 0x80u)
                return -1;
            code = (code << 6) | (uint32_t)(next & 0x3fu);
        }
        if ((extra == 1u && code < 0x80u) || (extra == 2u && code < 0x800u) ||
            (extra == 3u && code < 0x10000u))
            return -1;
        if (code > 0x10ffffu || (code >= 0xd800u && code <= 0xdfffu))
            return -1;
        index += extra + 1u;

        if (code >= 0x10000u) {
            uint32_t offset = code - 0x10000u;
            uint16_t high = (uint16_t)(0xd800u + (offset >> 10));
            uint16_t low = (uint16_t)(0xdc00u + (offset & 0x3ffu));
            if (used + 4u > capacity)
                return -1;
            dest[used++] = (uint8_t)(high >> 8);
            dest[used++] = (uint8_t)(high & 0xffu);
            dest[used++] = (uint8_t)(low >> 8);
            dest[used++] = (uint8_t)(low & 0xffu);
        } else {
            if (used + 2u > capacity)
                return -1;
            dest[used++] = (uint8_t)(code >> 8);
            dest[used++] = (uint8_t)(code & 0xffu);
        }
    }
    *units = used / 2u;
    return 0;
}

static int canonicalize(const char *text, struct name *out)
{
    uint8_t raw[UNIT_BYTES];
    size_t raw_units = 0u;
    if (utf8_to_utf16be(text, strlen(text), raw, sizeof(raw), &raw_units) != 0)
        return -1;

    size_t canon_units = 0u;
    int rc = hfs_unicode_normalize_name(raw, raw_units, out->units,
                                        sizeof(out->units), &canon_units);
    if (rc != HFS_OK)
        return -1;
    out->unit_count = canon_units;
    rc = hfs_utf16be_to_utf8(out->units, canon_units, out->display,
                             sizeof(out->display));
    return rc == HFS_OK ? 0 : -1;
}

static int compare_names(const void *left, const void *right)
{
    const struct name *a = left;
    const struct name *b = right;
    int rc = hfs_catalog_name_compare(a->units, a->unit_count, b->units,
                                      b->unit_count);
    if (rc != 0)
        return rc;
    /* Distinct names, but a deterministic tie-break costs nothing. */
    return strcmp(a->display, b->display);
}

static int read_names(int canon_only)
{
    names = calloc(MAX_NAMES, sizeof(*names));
    if (names == NULL) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }

    while (fgets(names[name_count].text, NAME_BYTES, stdin) != NULL) {
        size_t length = strlen(names[name_count].text);
        while (length > 0u && (names[name_count].text[length - 1u] == '\n' ||
                               names[name_count].text[length - 1u] == '\r'))
            names[name_count].text[--length] = '\0';
        if (length == 0u)
            continue;
        if (canonicalize(names[name_count].text, &names[name_count]) != 0) {
            fprintf(stderr, "cannot canonicalize %s\n",
                    names[name_count].text);
            return 1;
        }
        if (++name_count == MAX_NAMES) {
            fprintf(stderr, "more than %u names\n", (unsigned)MAX_NAMES);
            return 1;
        }
    }

    if (!canon_only)
        qsort(names, name_count, sizeof(*names), compare_names);

    for (size_t index = 0u; index < name_count; ++index) {
        if (printf("%s\n", names[index].display) < 0)
            return 1;
    }
    return fflush(stdout) == 0 ? 0 : 1;
}

static void usage(const char *program)
{
    fprintf(stderr,
            "usage: %s list IMAGE [PATH]\n"
            "       %s tree IMAGE [PATH]\n"
            "       %s read IMAGE PATH\n"
            "       %s sort < names\n"
            "       %s canon < names\n",
            program, program, program, program, program);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }

    if (strcmp(argv[1], "sort") == 0)
        return read_names(0);
    if (strcmp(argv[1], "canon") == 0)
        return read_names(1);

    if (strcmp(argv[1], "list") == 0 || strcmp(argv[1], "tree") == 0) {
        if (argc != 3 && argc != 4) {
            usage(argv[0]);
            return 2;
        }
        struct image image;
        if (open_image(argv[2], &image) != 0)
            return 2;
        int rc = list_tree(&image, argc == 4 ? argv[3] : "/",
                           strcmp(argv[1], "tree") == 0);
        close(image.fd);
        return rc;
    }

    if (strcmp(argv[1], "read") == 0) {
        if (argc != 4) {
            usage(argv[0]);
            return 2;
        }
        struct image image;
        if (open_image(argv[2], &image) != 0)
            return 2;
        int rc = read_path(&image, argv[3]);
        close(image.fd);
        return rc;
    }

    usage(argv[0]);
    return 2;
}
