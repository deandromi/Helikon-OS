/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Randomized write round-trip over a disposable HFS+ image.
 *
 *   hfs_ops_test IMAGE SEED OPS
 *
 * The hand-written image tests walk named paths: create, rename, truncate,
 * delete, overflow, grow. This one walks the paths nobody thought of. It drives
 * the catalog writer through a deterministic pseudo-random sequence of
 * create/append/truncate/rename/delete operations, keeps a model of what the
 * volume should hold, and after every single operation asks the read core
 * whether the volume agrees. The shell driver adds the two oracles this file
 * cannot be: `hfs-check` (the structural check) and `fsck_hfs`.
 *
 * A failure prints the seed and the operation index, and the sequence is
 * reproducible from those two numbers alone: every random draw comes from the
 * same LCG, seeded once.
 *
 * It never touches a device. The image must be one the driver just created.
 */
#include "hfs_bitmap.h"
#include "hfs_catalog.h"
#include "hfs_catalog_compare.h"
#include "hfs_catalog_write.h"
#include "hfs_extents.h"
#include "hfs_extents_write.h"
#include "hfs_journal.h"
#include "hfs_txn.h"
#include "hfs_volume.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MAX_SLOTS 16u
#define MAX_BLOCKS 3u
#define NAME_UNITS 8u
#define ROOT_FOLDER 2u

struct image {
    int fd;
    struct hfs_volume volume;
    struct hfs_journal journal;
    struct hfs_catalog catalog;
    uint8_t scratch[32768];
};

struct slot {
    int live;
    char name[24];
    uint32_t cnid;
    uint64_t size;
};

/* What the current operation asked for, so a failure reads as itself. */
static char op_description[160];

struct model {
    struct slot slots[MAX_SLOTS];
    uint32_t name_serial;
    uint32_t block_size;
    uint32_t seed;        /* the LCG state, reseeded per run */
    uint32_t initial;     /* the seed the run was started with, for reports */
    uint32_t op;
};

static uint32_t next_random(struct model *model)
{
    model->seed = model->seed * 1664525u + 1013904223u;
    return model->seed >> 8;
}

static int read_at(void *context, uint64_t offset, void *buffer, size_t length)
{
    int fd = *(int *)context;
    ssize_t done = pread(fd, buffer, length, (off_t)offset);
    return done == (ssize_t)length ? 0 : -1;
}

static int write_at(void *context, uint64_t offset, const void *buffer,
                    size_t length)
{
    int fd = *(int *)context;
    ssize_t done = pwrite(fd, buffer, length, (off_t)offset);
    return done == (ssize_t)length ? 0 : -1;
}

static int flush_at(void *context)
{
    int fd = *(int *)context;
    return fsync(fd) == 0 ? 0 : -1;
}

static int open_image(const char *path, struct image *image)
{
    memset(image, 0, sizeof(*image));
    image->fd = open(path, O_RDWR);
    if (image->fd < 0)
        return HFS_ERR_IO;

    off_t end = lseek(image->fd, 0, SEEK_END);
    if (end < 0)
        return HFS_ERR_IO;
    int rc = hfs_volume_probe(&image->volume, read_at, &image->fd, 0,
                              (uint64_t)end);
    if (rc != HFS_OK && rc != HFS_ERR_DIRTY)
        return rc;
    if (!image->volume.journaled)
        return HFS_ERR_UNSUPPORTED;

    rc = hfs_journal_open(&image->journal, &image->volume, write_at, flush_at);
    if (rc != HFS_OK)
        return rc;
    if (!hfs_journal_is_clean(&image->journal)) {
        rc = hfs_journal_replay(&image->journal);
        if (rc != HFS_OK)
            return rc;
        rc = hfs_volume_probe(&image->volume, read_at, &image->fd, 0,
                              (uint64_t)end);
        if (rc != HFS_OK)
            return rc;
    }
    rc = hfs_catalog_open(&image->catalog, &image->volume, image->scratch,
                          sizeof(image->scratch));
    if (rc != HFS_OK)
        return rc;
    time_t now = time(NULL);
    if (now > 0)
        image->catalog.now = (uint32_t)((int64_t)now + 2082844800LL);
    return HFS_OK;
}

static void close_image(struct image *image)
{
    if (image->fd >= 0)
        close(image->fd);
}

static int commit(struct hfs_txn *tx, int rc)
{
    if (rc == HFS_OK)
        return hfs_txn_commit(tx);
    hfs_txn_abort(tx);
    return rc;
}

static size_t name_units(const char *name, uint8_t *out)
{
    size_t length = strlen(name);
    for (size_t index = 0; index < length; ++index) {
        out[index * 2u] = 0u;
        out[index * 2u + 1u] = (uint8_t)name[index];
    }
    return length;
}

/*
 * The model is the oracle for every operation: whatever the writer did, a fresh
 * lookup has to agree with it. This is checked after each step, not at the end,
 * so the failure names the operation that broke it.
 */
static int verify_model(struct image *image, const struct model *model)
{
    for (uint32_t index = 0; index < MAX_SLOTS; ++index) {
        const struct slot *slot = &model->slots[index];
        struct hfs_entry entry;
        if (slot->name[0] == '\0')
            continue;   /* a slot the run never used */
        int rc = hfs_catalog_lookup_ascii(&image->catalog, ROOT_FOLDER,
                                          slot->name, &entry);
        if (slot->live == 0) {
            if (rc != HFS_ERR_NOT_FOUND) {
                fprintf(stderr, "seed %u op %u: %s should be gone\n",
                        model->initial, model->op, slot->name);
                return -1;
            }
            continue;
        }
        if (rc != HFS_OK) {
            fprintf(stderr, "seed %u op %u: %s missing (%s)\n",
                    model->initial, model->op, slot->name, hfs_strerror(rc));
            return -1;
        }
        if (entry.kind != 2u || entry.data.size != slot->size) {
            fprintf(stderr,
                    "seed %u op %u: %s is kind %u size %llu, model says 2/%llu\n",
                    model->initial, model->op, slot->name,
                    (unsigned)entry.kind,
                    (unsigned long long)entry.data.size,
                    (unsigned long long)slot->size);
            return -1;
        }
    }
    return 0;
}

/*
 * Every block a live file claims must be marked allocated in the bitmap, and no
 * block may belong to two files. A fork that reaches a free block is the seed of
 * silent data loss: something else allocates it later and the two writers
 * overwrite each other. Checked after every operation, so the failure names the
 * step that broke it.
 */
static int check_blocks(struct image *image, const struct model *model)
{
    struct hfs_bitmap bitmap;
    int rc = hfs_bitmap_open(&bitmap, &image->volume);
    if (rc != HFS_OK) {
        fprintf(stderr, "seed %u op %u: bitmap open: %s\n", model->initial,
                model->op, hfs_strerror(rc));
        return -1;
    }

    uint32_t owner_start[MAX_SLOTS * 2u * HFS_EXTENT_COUNT];
    uint32_t owner_count[MAX_SLOTS * 2u * HFS_EXTENT_COUNT];
    uint32_t owner_slot[MAX_SLOTS * 2u * HFS_EXTENT_COUNT];
    size_t owners = 0;

    for (uint32_t index = 0; index < MAX_SLOTS; ++index) {
        const struct slot *slot = &model->slots[index];
        if (slot->live == 0 || slot->name[0] == '\0')
            continue;
        struct hfs_entry entry;
        rc = hfs_catalog_lookup_ascii(&image->catalog, ROOT_FOLDER, slot->name,
                                      &entry);
        if (rc != HFS_OK) {
            fprintf(stderr, "seed %u op %u: lookup %s: %s\n", model->initial,
                    model->op, slot->name, hfs_strerror(rc));
            return -1;
        }

        /* Each file gets its own read transaction: the loop aborts it. */
        struct hfs_txn tx;
        rc = hfs_txn_begin(&tx, &image->journal);
        if (rc != HFS_OK) {
            fprintf(stderr, "seed %u op %u: transaction: %s\n", model->initial,
                    model->op, hfs_strerror(rc));
            return -1;
        }

        struct hfs_extent map[8u * HFS_EXTENT_COUNT];
        size_t map_count = 0;
        uint32_t inline_blocks = 0;
        for (unsigned int i = 0; i < HFS_EXTENT_COUNT; ++i) {
            map[map_count++] = entry.data.extents[i];
            inline_blocks += entry.data.extents[i].count;
        }
        uint32_t logical = inline_blocks;
        while (logical < entry.data.blocks) {
            struct hfs_extent extents[HFS_EXTENT_COUNT];
            size_t got = 0;
            rc = hfs_extents_overflow_get(&image->volume, &tx, entry.id,
                                          logical, extents, HFS_EXTENT_COUNT,
                                          &got);
            if (rc != HFS_OK || got == 0) {
                fprintf(stderr,
                        "seed %u op %u: %s has no overflow record at logical "
                        "%u (%s, got %u) but claims %u blocks\n",
                        model->initial, model->op, slot->name,
                        (unsigned)logical, hfs_strerror(rc), (unsigned)got,
                        (unsigned)entry.data.blocks);
                hfs_txn_abort(&tx);
                return -1;
            }
            uint32_t sum = 0;
            for (size_t i = 0; i < got; ++i) {
                map[map_count++] = extents[i];
                sum += extents[i].count;
            }
            logical += sum;
        }
        hfs_txn_abort(&tx);

        for (size_t i = 0; i < map_count; ++i) {
            if (map[i].count == 0)
                continue;
            /*
             * Compare against the extents already seen — never against this
             * one — so a run of blocks does not collide with itself. The list
             * is extended once, after the whole extent has been checked.
             */
            for (uint32_t b = 0; b < map[i].count; ++b) {
                uint32_t block = map[i].start + b;
                int used = 0;
                rc = hfs_bitmap_block_used(&bitmap, block, &used);
                if (rc != HFS_OK) {
                    fprintf(stderr,
                            "seed %u op %u: block %u query: %s\n",
                            model->initial, model->op, (unsigned)block,
                            hfs_strerror(rc));
                    return -1;
                }
                if (used == 0) {
                    fprintf(stderr,
                            "seed %u op %u: %s claims block %u, bitmap says "
                            "free\n", model->initial, model->op, slot->name,
                            (unsigned)block);
                    return -1;
                }
                for (size_t k = 0; k < owners; ++k) {
                    uint32_t first = owner_start[k];
                    uint32_t last = first + owner_count[k];
                    if (block >= first && block < last) {
                        fprintf(stderr,
                                "seed %u op %u: block %u belongs to %s and "
                                "slot %u\n", model->initial, model->op,
                                (unsigned)block, slot->name,
                                (unsigned)owner_slot[k]);
                        return -1;
                    }
                }
            }
            if (owners >= sizeof(owner_start) / sizeof(owner_start[0])) {
                fprintf(stderr, "seed %u op %u: too many extents to track\n",
                        model->initial, model->op);
                return -1;
            }
            owner_start[owners] = map[i].start;
            owner_count[owners] = map[i].count;
            owner_slot[owners] = index;
            owners += 1u;
        }
    }
    return 0;
}

static uint32_t live_count(const struct model *model)
{
    uint32_t live = 0;
    for (uint32_t index = 0; index < MAX_SLOTS; ++index)
        live += model->slots[index].live != 0;
    return live;
}

static int pick_live(struct model *model)
{
    uint32_t live = live_count(model);
    if (live == 0)
        return -1;
    uint32_t target = next_random(model) % live;
    for (uint32_t index = 0; index < MAX_SLOTS; ++index) {
        if (model->slots[index].live == 0)
            continue;
        if (target == 0)
            return (int)index;
        target -= 1u;
    }
    return -1;
}

static int pick_free(struct model *model)
{
    for (uint32_t index = 0; index < MAX_SLOTS; ++index) {
        if (model->slots[index].live == 0)
            return (int)index;
    }
    return -1;
}

static void fresh_name(struct model *model, char *out, size_t capacity)
{
    snprintf(out, capacity, "n%u", model->name_serial);
    model->name_serial += 1u;
}

static int do_create(struct image *image, struct model *model, int slot_index)
{
    struct slot *slot = &model->slots[slot_index];
    fresh_name(model, slot->name, sizeof(slot->name));
    uint8_t name[NAME_UNITS * 2u];
    size_t units = name_units(slot->name, name);

    snprintf(op_description, sizeof(op_description), "create %s", slot->name);
    struct hfs_txn tx;
    int rc = hfs_txn_begin(&tx, &image->journal);
    if (rc != HFS_OK)
        return rc;
    rc = commit(&tx, hfs_catalog_create_file(&image->catalog, &tx, ROOT_FOLDER,
                                             name, units, 0644,
                                             hfs_catalog_key_compare, NULL,
                                             &slot->cnid));
    if (rc != HFS_OK)
        return rc;
    slot->live = 1;
    slot->size = 0u;
    return HFS_OK;
}

static int do_append(struct image *image, struct model *model, int slot_index)
{
    struct slot *slot = &model->slots[slot_index];
    uint8_t name[NAME_UNITS * 2u];
    size_t units = name_units(slot->name, name);
    uint32_t blocks = 1u + next_random(model) % MAX_BLOCKS;
    static uint8_t buffer[MAX_BLOCKS * 4096u];
    size_t length = (size_t)blocks * model->block_size;
    if (length > sizeof(buffer))
        length = sizeof(buffer);
    memset(buffer, (int)(0x40u + (uint32_t)slot_index), length);

    snprintf(op_description, sizeof(op_description),
             "append %s offset %llu length %zu", slot->name,
             (unsigned long long)slot->size, length);
    struct hfs_txn tx;
    int rc = hfs_txn_begin(&tx, &image->journal);
    if (rc != HFS_OK)
        return rc;
    rc = commit(&tx, hfs_catalog_append_file(&image->catalog, &tx, ROOT_FOLDER,
                                             name, units, slot->size, buffer,
                                             length, hfs_catalog_key_compare,
                                             NULL));
    if (rc != HFS_OK)
        return rc;
    slot->size += length;
    return HFS_OK;
}

static int do_truncate(struct image *image, struct model *model, int slot_index)
{
    struct slot *slot = &model->slots[slot_index];
    if (slot->size == 0u)
        return HFS_OK;
    uint64_t target = next_random(model) % (slot->size + 1u);
    snprintf(op_description, sizeof(op_description), "truncate %s %llu to %llu",
             slot->name, (unsigned long long)slot->size,
             (unsigned long long)target);
    uint8_t name[NAME_UNITS * 2u];
    size_t units = name_units(slot->name, name);

    struct hfs_txn tx;
    int rc = hfs_txn_begin(&tx, &image->journal);
    if (rc != HFS_OK)
        return rc;
    rc = commit(&tx, hfs_catalog_truncate_to(&image->catalog, &tx, ROOT_FOLDER,
                                             name, units, target,
                                             hfs_catalog_key_compare, NULL));
    if (rc != HFS_OK)
        return rc;
    slot->size = target;
    return HFS_OK;
}

static int do_rename(struct image *image, struct model *model, int slot_index)
{
    struct slot *slot = &model->slots[slot_index];
    char renamed[24];
    fresh_name(model, renamed, sizeof(renamed));
    uint8_t old_name[NAME_UNITS * 2u];
    uint8_t new_name[NAME_UNITS * 2u];
    size_t old_units = name_units(slot->name, old_name);
    size_t new_units = name_units(renamed, new_name);

    snprintf(op_description, sizeof(op_description), "rename %s to %s",
             slot->name, renamed);
    struct hfs_txn tx;
    int rc = hfs_txn_begin(&tx, &image->journal);
    if (rc != HFS_OK)
        return rc;
    rc = commit(&tx, hfs_catalog_rename(&image->catalog, &tx, ROOT_FOLDER,
                                        old_name, old_units, slot->cnid,
                                        ROOT_FOLDER, new_name, new_units,
                                        hfs_catalog_key_compare, NULL));
    if (rc != HFS_OK)
        return rc;
    memcpy(slot->name, renamed, sizeof(slot->name));
    return HFS_OK;
}

static int do_delete(struct image *image, struct model *model, int slot_index)
{
    struct slot *slot = &model->slots[slot_index];
    uint8_t name[NAME_UNITS * 2u];
    size_t units = name_units(slot->name, name);

    snprintf(op_description, sizeof(op_description), "delete %s cnid %u",
             slot->name, (unsigned)slot->cnid);
    struct hfs_txn tx;
    int rc = hfs_txn_begin(&tx, &image->journal);
    if (rc != HFS_OK)
        return rc;
    rc = commit(&tx, hfs_catalog_delete_file(&image->catalog, &tx, ROOT_FOLDER,
                                             name, units, slot->cnid,
                                             hfs_catalog_key_compare, NULL));
    if (rc != HFS_OK)
        return rc;
    slot->live = 0;
    slot->size = 0u;
    return HFS_OK;
}

/*
 * Five operations, weighted so the volume fills and empties instead of running
 * to one steady state: creates matter most while there is room, appends and
 * truncates move the fork shapes, deletes and renames churn the B-tree keys.
 */
static int run_operation(struct image *image, struct model *model)
{
    uint32_t draw = next_random(model) % 100u;
    int live = pick_live(model);
    int free_slot = pick_free(model);

    if (draw < 30u && free_slot >= 0)
        return do_create(image, model, free_slot);
    if (draw < 65u && live >= 0)
        return do_append(image, model, live);
    if (draw < 80u && live >= 0)
        return do_truncate(image, model, live);
    if (draw < 90u && live >= 0)
        return do_rename(image, model, live);
    if (live >= 0)
        return do_delete(image, model, live);
    if (free_slot >= 0)
        return do_create(image, model, free_slot);
    return HFS_OK;
}

/*
 * `dump NAME` prints a file's fork and the extents-overflow records that cover
 * it, so a failure the model catches can be read instead of guessed at.
 */
static int dump_file(struct image *image, const char *name)
{
    struct hfs_entry entry;
    int rc = hfs_catalog_lookup_ascii(&image->catalog, ROOT_FOLDER, name,
                                      &entry);
    if (rc != HFS_OK) {
        fprintf(stderr, "dump: %s: %s\n", name, hfs_strerror(rc));
        return 1;
    }
    printf("%s: kind %u size %llu blocks %u\n", name, (unsigned)entry.kind,
           (unsigned long long)entry.data.size, (unsigned)entry.data.blocks);
    uint32_t inline_blocks = 0;
    for (unsigned int i = 0; i < HFS_EXTENT_COUNT; ++i) {
        printf("  inline[%u] start %u count %u\n", i,
               (unsigned)entry.data.extents[i].start,
               (unsigned)entry.data.extents[i].count);
        inline_blocks += entry.data.extents[i].count;
    }
    printf("  inline blocks %u\n", (unsigned)inline_blocks);

    struct hfs_txn tx;
    rc = hfs_txn_begin(&tx, &image->journal);
    if (rc != HFS_OK)
        return 1;
    uint32_t logical = inline_blocks;
    while (logical < entry.data.blocks) {
        struct hfs_extent extents[HFS_EXTENT_COUNT];
        size_t got = 0;
        rc = hfs_extents_overflow_get(&image->volume, &tx, entry.id, logical,
                                      extents, HFS_EXTENT_COUNT, &got);
        printf("  overflow at logical %u: rc %s got %u\n", (unsigned)logical,
               hfs_strerror(rc), (unsigned)got);
        if (rc != HFS_OK || got == 0)
            break;
        uint32_t sum = 0;
        for (size_t i = 0; i < got; ++i) {
            printf("    [%zu] start %u count %u\n", i,
                   (unsigned)extents[i].start, (unsigned)extents[i].count);
            sum += extents[i].count;
        }
        if (sum == 0)
            break;
        logical += sum;
    }
    hfs_txn_abort(&tx);
    return rc == HFS_OK ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc == 4 && strcmp(argv[2], "dump") == 0) {
        struct image dump_image;
        int dump_rc = open_image(argv[1], &dump_image);
        if (dump_rc != HFS_OK) {
            fprintf(stderr, "open: %s\n", hfs_strerror(dump_rc));
            return 1;
        }
        dump_rc = dump_file(&dump_image, argv[3]);
        close_image(&dump_image);
        return dump_rc;
    }
    if (argc != 4) {
        fprintf(stderr, "usage: %s IMAGE SEED OPS\n"
                        "       %s IMAGE dump NAME\n",
                argv[0], argv[0]);
        return 2;
    }
    char *end = NULL;
    unsigned long seed = strtoul(argv[2], &end, 0);
    if (end == NULL || *end != '\0' || seed == 0u) {
        fprintf(stderr, "seed must be a non-zero integer\n");
        return 2;
    }
    end = NULL;
    unsigned long ops = strtoul(argv[3], &end, 0);
    if (end == NULL || *end != '\0' || ops == 0u) {
        fprintf(stderr, "ops must be a non-zero integer\n");
        return 2;
    }

    struct image image;
    int rc = open_image(argv[1], &image);
    if (rc != HFS_OK) {
        fprintf(stderr, "open: %s\n", hfs_strerror(rc));
        return 1;
    }

    struct model model;
    memset(&model, 0, sizeof(model));
    model.seed = (uint32_t)seed;
    model.initial = (uint32_t)seed;
    model.block_size = image.volume.block_size;

    for (unsigned long step = 0; step < ops; ++step) {
        model.op = (uint32_t)step;
        rc = run_operation(&image, &model);
        if (rc != HFS_OK) {
            fprintf(stderr, "seed %lu op %lu: %s: %s\n", seed, step,
                    op_description, hfs_strerror(rc));
            close_image(&image);
            return 1;
        }
        if (verify_model(&image, &model) != 0)
            goto failed;
        if (check_blocks(&image, &model) != 0)
            goto failed;
        if (getenv("HFS_OPS_VERBOSE") != NULL)
            fprintf(stderr, "op %lu ok: %s\n", step, op_description);
    }

    uint32_t live = live_count(&model);
    printf("seed %lu: %lu ops, %u files live\n", seed, ops, (unsigned)live);
    close_image(&image);
    return 0;

failed:
    close_image(&image);
    return 1;
}
