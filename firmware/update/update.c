/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The signed staged-update client. See update.h for the phases and the rules;
 * this file is the implementation and the comments explain the parts that are
 * easy to get wrong.
 */
#include "update.h"

#include <stddef.h>
#include <stdint.h>

#include "bootstate.h"
#include "helikon_error.h"
#include "loader_error.h"
#include "mem.h"
#include "sha256.h"

#define MANIFEST_BYTES HELIKON_UPDATE_MANIFEST_BYTES
#define BOOTSTATE_BYTES HELIKON_UPDATE_BOOTSTATE_BYTES

/* One file's worth of facts, so the helpers below stay honest about sizes. */
struct file_facts {
    uint64_t size;
    uint8_t digest[HELIKON_SHA256_SIZE];
};

/*
 * The manifest verifier keeps its own error vocabulary (the loader's), because
 * it is the loader's code. Everything a caller sees is the firmware taxonomy:
 *
 *   a set that is not a Helikon set: FORMAT.
 *   another board, an unknown key id, a bad signature, a development set this
 *   build may not install: UNSUPPORTED — the set is well formed and this device
 *   is not allowed to install it.
 */
static int map_manifest_error(int rc)
{
    switch (rc) {
    case HELIKON_LOADER_OK:
        return HELIKON_OK;
    case HELIKON_LOADER_ERR_ARGUMENT:
        return HELIKON_ERR_ARGUMENT;
    case HELIKON_LOADER_ERR_BOARD:
    case HELIKON_LOADER_ERR_SIGNATURE:
    case HELIKON_LOADER_ERR_FLAGS:
        return HELIKON_ERR_UNSUPPORTED;
    default:
        return HELIKON_ERR_FORMAT;
    }
}

static int file_size(struct helikon_update *update, const char *path,
                     uint64_t *size)
{
    struct vfs_entry entry;
    int rc = vfs_stat(update->vfs, path, &entry);
    if (rc != HELIKON_OK)
        return rc;
    if (entry.kind != VFS_KIND_FILE)
        return HELIKON_ERR_FORMAT;
    *size = entry.size;
    return HELIKON_OK;
}

/* Reads exactly `length` bytes at `offset` into `update->chunk`. */
static int read_chunk(struct helikon_update *update, const char *path,
                      uint64_t offset, size_t length)
{
    struct vfs_file_handle handle;
    int rc = vfs_file_open(update->vfs, path, &handle);
    if (rc != HELIKON_OK)
        return rc;

    if (offset != 0u) {
        uint64_t position = 0u;
        rc = vfs_file_seek(update->vfs, handle, VFS_SEEK_SET,
                           (int64_t)offset, &position);
        if (rc != HELIKON_OK) {
            (void)vfs_file_close(update->vfs, handle);
            return rc;
        }
    }

    size_t total = 0u;
    while (total < length) {
        size_t done = 0u;
        rc = vfs_file_read(update->vfs, handle, update->chunk + total,
                           length - total, &done);
        if (rc != HELIKON_OK) {
            (void)vfs_file_close(update->vfs, handle);
            return rc;
        }
        if (done == 0u) {
            (void)vfs_file_close(update->vfs, handle);
            return HELIKON_ERR_FORMAT; /* the file is shorter than claimed */
        }
        total += done;
    }
    (void)vfs_file_close(update->vfs, handle);
    return HELIKON_OK;
}

/* Streams a whole file through the SHA-256 state; no size cap, one chunk. */
static int hash_file(struct helikon_update *update, const char *path,
                     struct file_facts *facts)
{
    struct vfs_file_handle handle;
    int rc = vfs_file_open(update->vfs, path, &handle);
    if (rc != HELIKON_OK)
        return rc;

    struct helikon_sha256 ctx;
    helikon_sha256_init(&ctx);
    uint64_t total = 0u;
    for (;;) {
        size_t done = 0u;
        rc = vfs_file_read(update->vfs, handle, update->chunk,
                           sizeof(update->chunk), &done);
        if (rc != HELIKON_OK) {
            (void)vfs_file_close(update->vfs, handle);
            return rc;
        }
        if (done == 0u)
            break;
        helikon_sha256_update(&ctx, update->chunk, done);
        total += done;
    }
    (void)vfs_file_close(update->vfs, handle);
    helikon_sha256_final(&ctx, facts->digest);
    facts->size = total;
    return HELIKON_OK;
}

static int matches(const struct file_facts *facts, uint64_t size,
                   const uint8_t digest[HELIKON_SHA256_SIZE])
{
    return facts->size == size &&
           helikon_memcmp(facts->digest, digest, HELIKON_SHA256_SIZE) == 0;
}

/* Reads a fixed-size file whole: the manifest and the boot state are records. */
static int read_record(struct helikon_update *update, const char *path,
                       uint8_t *buffer, size_t length)
{
    uint64_t size = 0u;
    int rc = file_size(update, path, &size);
    if (rc != HELIKON_OK)
        return rc;
    if (size != (uint64_t)length)
        return HELIKON_ERR_FORMAT;
    if (length > sizeof(update->chunk))
        return HELIKON_ERR_ARGUMENT;
    rc = read_chunk(update, path, 0u, length);
    if (rc != HELIKON_OK)
        return rc;
    helikon_memcpy(buffer, update->chunk, length);
    return HELIKON_OK;
}

/* Makes sure a directory exists. BUSY means it is already there. */
static int ensure_dir(struct helikon_update *update, const char *path)
{
    struct vfs_entry entry;
    int rc = vfs_stat(update->vfs, path, &entry);
    if (rc == HELIKON_OK) {
        if (entry.kind != VFS_KIND_DIR)
            return HELIKON_ERR_FORMAT;
        return HELIKON_OK;
    }
    if (rc != HELIKON_ERR_NOT_FOUND)
        return rc;
    rc = vfs_rw_mkdir(update->rw, path, NULL);
    if (rc == HELIKON_ERR_BUSY)
        return HELIKON_OK;
    return rc;
}

/*
 * Copies `from` to a fresh `to`, then reads `to` back and requires the exact
 * size and digest. The destination is removed first: a copy must never inherit
 * the tail of a longer file it is replacing, and a failed copy has to leave
 * "missing or short", never "old bytes with new bytes on top".
 *
 * The source is re-opened for every chunk on purpose. Each write is a commit,
 * and a commit invalidates every open handle including the one this copy is
 * reading from.
 */
static int copy_to(struct helikon_update *update, const char *from,
                   uint64_t size, const char *to,
                   const uint8_t digest[HELIKON_SHA256_SIZE])
{
    int rc = vfs_rw_unlink(update->rw, to);
    if (rc != HELIKON_OK && rc != HELIKON_ERR_NOT_FOUND)
        return rc;
    rc = vfs_rw_create(update->rw, to, NULL);
    if (rc != HELIKON_OK)
        return rc;

    uint64_t offset = 0u;
    while (offset < size) {
        uint64_t left = size - offset;
        size_t want = left > HELIKON_UPDATE_CHUNK ? HELIKON_UPDATE_CHUNK
                                                  : (size_t)left;
        rc = read_chunk(update, from, offset, want);
        if (rc != HELIKON_OK)
            return rc;

        size_t done = 0u;
        while (done < want) {
            size_t applied = 0u;
            rc = vfs_rw_write(update->rw, to, offset + done,
                              update->chunk + done, want - done, &applied);
            if (rc != HELIKON_OK)
                return rc;
            if (applied == 0u)
                return HELIKON_ERR_IO;
            done += applied;
        }
        offset += want;
    }

    struct file_facts facts;
    rc = hash_file(update, to, &facts);
    if (rc != HELIKON_OK)
        return rc;
    if (facts.size != size)
        return HELIKON_ERR_IO;
    /* `digest` is NULL when the caller compares the bytes itself (a manifest,
     * whose digest is not a fact the manifest carries). */
    if (digest != NULL &&
        helikon_memcmp(facts.digest, digest, HELIKON_SHA256_SIZE) != 0)
        return HELIKON_ERR_IO;
    return HELIKON_OK;
}

int helikon_update_open(struct helikon_update *update, struct vfs *vfs,
                        struct vfs_rw *rw,
                        const struct helikon_key_table *keys, uint32_t board,
                        int allow_development)
{
    if (update == NULL || vfs == NULL || keys == NULL || board == 0u)
        return HELIKON_ERR_ARGUMENT;

    helikon_memset(update, 0, sizeof(*update));
    update->vfs = vfs;
    update->rw = rw;
    update->keys = keys;
    update->board = board;
    update->allow_development = allow_development != 0 ? 1 : 0;
    update->stage = HELIKON_UPDATE_IDLE;
    return HELIKON_OK;
}

int helikon_update_verify(struct helikon_update *update)
{
    if (update == NULL || update->vfs == NULL || update->keys == NULL)
        return HELIKON_ERR_ARGUMENT;

    update->stage = HELIKON_UPDATE_IDLE;
    update->have_installed = 0;

    int rc = read_record(update, HELIKON_UPDATE_STAGED_MANIFEST,
                         update->manifest_bytes, MANIFEST_BYTES);
    if (rc != HELIKON_OK)
        return rc;

    rc = map_manifest_error(helikon_manifest_verify_signature(
        update->manifest_bytes, MANIFEST_BYTES, update->board,
        update->allow_development, helikon_key_table_verify,
        (void *)update->keys, &update->staged));
    if (rc != HELIKON_OK)
        return rc;

    /*
     * The installed manifest is read for two reasons and trusted for neither:
     * it bounds the image version from below, and it is what the recovery copy
     * duplicates. A manifest that does not verify simply is not one — the update
     * still runs, because refusing here would leave a device whose manifest was
     * overwritten with no way to install its way out.
     */
    uint64_t installed_size = 0u;
    if (file_size(update, HELIKON_UPDATE_INSTALLED_MANIFEST,
                  &installed_size) == HELIKON_OK &&
        installed_size == MANIFEST_BYTES) {
        uint8_t installed[MANIFEST_BYTES];
        if (read_record(update, HELIKON_UPDATE_INSTALLED_MANIFEST, installed,
                        MANIFEST_BYTES) == HELIKON_OK) {
            struct helikon_manifest parsed;
            if (map_manifest_error(helikon_manifest_verify_signature(
                    installed, MANIFEST_BYTES, update->board,
                    update->allow_development, helikon_key_table_verify,
                    (void *)update->keys, &parsed)) == HELIKON_OK) {
                update->installed = parsed;
                helikon_memcpy(update->installed_bytes, installed,
                               MANIFEST_BYTES);
                update->have_installed = 1;
            }
        }
    }

    if (update->have_installed != 0 &&
        update->staged.image_version <= update->installed.image_version)
        return HELIKON_ERR_ARGUMENT;

    uint64_t size = 0u;
    rc = file_size(update, HELIKON_UPDATE_STAGED_IMAGE, &size);
    if (rc != HELIKON_OK)
        return rc;
    if (size != (uint64_t)update->staged.image_size)
        return HELIKON_ERR_FORMAT;

    struct file_facts facts;
    rc = hash_file(update, HELIKON_UPDATE_STAGED_IMAGE, &facts);
    if (rc != HELIKON_OK)
        return rc;
    if (!matches(&facts, update->staged.image_size,
                 update->staged.image_sha256))
        return HELIKON_ERR_FORMAT;

    update->stage = HELIKON_UPDATE_VERIFIED;
    return HELIKON_OK;
}

int helikon_update_park(struct helikon_update *update)
{
    if (update == NULL || update->vfs == NULL || update->rw == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (update->stage != HELIKON_UPDATE_VERIFIED)
        return HELIKON_ERR_ARGUMENT;
    if (update->rw->writable == 0u)
        return HELIKON_ERR_READ_ONLY;

    int rc = ensure_dir(update, HELIKON_UPDATE_ROOT);
    if (rc != HELIKON_OK)
        return rc;
    rc = ensure_dir(update, HELIKON_UPDATE_STAGING);
    if (rc != HELIKON_OK)
        return rc;

    /*
     * The recovery copy is a copy of what is installed now, and it is written
     * before anything else so a later failure still leaves the user something to
     * restore from.
     */
    if (update->have_installed != 0) {
        rc = ensure_dir(update, HELIKON_UPDATE_RECOVERY);
        if (rc != HELIKON_OK)
            return rc;
        rc = copy_to(update, HELIKON_UPDATE_INSTALLED_IMAGE,
                     update->installed.image_size,
                     HELIKON_UPDATE_RECOVERY_IMAGE,
                     update->installed.image_sha256);
        if (rc != HELIKON_OK)
            return rc;
        /* The manifest is compared byte for byte below; there is no digest of
         * a manifest to check it against. */
        rc = copy_to(update, HELIKON_UPDATE_INSTALLED_MANIFEST, MANIFEST_BYTES,
                     HELIKON_UPDATE_RECOVERY_MANIFEST, NULL);
        if (rc != HELIKON_OK)
            return rc;
        uint8_t check[MANIFEST_BYTES];
        rc = read_record(update, HELIKON_UPDATE_RECOVERY_MANIFEST, check,
                         MANIFEST_BYTES);
        if (rc != HELIKON_OK)
            return rc;
        if (helikon_memcmp(check, update->installed_bytes, MANIFEST_BYTES) != 0)
            return HELIKON_ERR_IO;
    }

    /* Park the staged image beside the installed one. Nothing is swapped yet. */
    rc = copy_to(update, HELIKON_UPDATE_STAGED_IMAGE,
                 update->staged.image_size, HELIKON_UPDATE_PARKED_IMAGE,
                 update->staged.image_sha256);
    if (rc != HELIKON_OK)
        return rc;

    update->stage = HELIKON_UPDATE_PARKED;
    return HELIKON_OK;
}

int helikon_update_commit(struct helikon_update *update)
{
    if (update == NULL || update->vfs == NULL || update->rw == NULL)
        return HELIKON_ERR_ARGUMENT;
    if (update->stage != HELIKON_UPDATE_PARKED)
        return HELIKON_ERR_ARGUMENT;
    if (update->rw->writable == 0u)
        return HELIKON_ERR_READ_ONLY;

    /* The boot state is read before the commit: it names what the loader chose
     * last boot, and that is what decides which slot this update replaces. */
    uint8_t bootstate[BOOTSTATE_BYTES];
    uint64_t bootstate_size = 0u;
    int have_bootstate = 0;
    if (file_size(update, HELIKON_UPDATE_BOOTSTATE, &bootstate_size) ==
            HELIKON_OK &&
        bootstate_size == BOOTSTATE_BYTES &&
        read_record(update, HELIKON_UPDATE_BOOTSTATE, bootstate,
                    BOOTSTATE_BYTES) == HELIKON_OK)
        have_bootstate = 1;
    if (have_bootstate == 0)
        helikon_memset(bootstate, 0, sizeof(bootstate));

    uint32_t sequence = 0u;
    int rc = helikon_bootstate_stage_trying(
        bootstate, sizeof(bootstate), update->board,
        update->staged.image_version, update->staged.load_address,
        update->staged.image_size, update->staged.image_sha256, &sequence);
    if (rc != HELIKON_OK)
        return rc;

    /* Whether an installed image exists is decided before the scope opens: a
     * refusal inside a scope ends it, so the commit must not ask a question it
     * can answer first. */
    uint64_t installed_image_size = 0u;
    int have_installed_image =
        file_size(update, HELIKON_UPDATE_INSTALLED_IMAGE,
                  &installed_image_size) == HELIKON_OK;

    rc = vfs_rw_txn_begin(update->rw);
    if (rc != HELIKON_OK)
        return rc;

    /* The old image goes away in the same commit that installs the new one, so
     * the loader never sees a manifest and an image that disagree. */
    rc = HELIKON_OK;
    if (have_installed_image != 0)
        rc = vfs_rw_txn_delete(update->rw, HELIKON_UPDATE_INSTALLED_IMAGE);
    if (rc == HELIKON_OK)
        rc = vfs_rw_txn_rename(update->rw, HELIKON_UPDATE_PARKED_IMAGE,
                               HELIKON_UPDATE_INSTALLED_IMAGE);
    if (rc == HELIKON_OK)
        rc = vfs_rw_txn_replace(update->rw, HELIKON_UPDATE_INSTALLED_MANIFEST,
                                update->manifest_bytes, MANIFEST_BYTES);
    if (rc == HELIKON_OK)
        rc = vfs_rw_txn_replace(update->rw, HELIKON_UPDATE_BOOTSTATE, bootstate,
                                BOOTSTATE_BYTES);
    if (rc != HELIKON_OK) {
        /* A failed operation already ended the scope; this is belt and braces
         * for a failure that happens between operations. */
        vfs_rw_txn_abort(update->rw);
        return rc;
    }

    rc = vfs_rw_txn_commit(update->rw);
    if (rc != HELIKON_OK)
        return rc;

    /*
     * Read all three back from the medium. A commit that reported success and a
     * medium that disagrees is the one result this project refuses to smooth
     * over: report IO, do not retry, do not "repair".
     */
    uint8_t manifest_check[MANIFEST_BYTES];
    rc = read_record(update, HELIKON_UPDATE_INSTALLED_MANIFEST,
                     manifest_check, MANIFEST_BYTES);
    if (rc != HELIKON_OK)
        return HELIKON_ERR_IO;
    if (helikon_memcmp(manifest_check, update->manifest_bytes,
                       MANIFEST_BYTES) != 0)
        return HELIKON_ERR_IO;

    struct file_facts facts;
    rc = hash_file(update, HELIKON_UPDATE_INSTALLED_IMAGE, &facts);
    if (rc != HELIKON_OK)
        return HELIKON_ERR_IO;
    if (!matches(&facts, update->staged.image_size,
                 update->staged.image_sha256))
        return HELIKON_ERR_IO;

    uint8_t bootstate_check[BOOTSTATE_BYTES];
    rc = read_record(update, HELIKON_UPDATE_BOOTSTATE, bootstate_check,
                     BOOTSTATE_BYTES);
    if (rc != HELIKON_OK)
        return HELIKON_ERR_IO;
    if (helikon_memcmp(bootstate_check, bootstate, BOOTSTATE_BYTES) != 0)
        return HELIKON_ERR_IO;

    struct helikon_bootstate parsed;
    rc = helikon_bootstate_parse(bootstate_check, sizeof(bootstate_check),
                                 update->board, &parsed);
    if (rc != HELIKON_OK)
        return HELIKON_ERR_IO;
    int staged_slot = 0;
    for (unsigned int i = 0; i < HELIKON_BOOTSTATE_SLOTS; ++i) {
        const struct helikon_bootstate_slot *slot = &parsed.slots[i];
        if (slot->valid &&
            slot->state == HELIKON_BOOTSTATE_TRYING &&
            slot->image_version == update->staged.image_version &&
            slot->load_address == update->staged.load_address &&
            slot->image_size == update->staged.image_size &&
            helikon_memcmp(slot->image_sha256, update->staged.image_sha256,
                           HELIKON_SHA256_SIZE) == 0) {
            staged_slot = 1;
            break;
        }
    }
    if (staged_slot == 0)
        return HELIKON_ERR_IO;

    update->bootstate_sequence = sequence;
    update->stage = HELIKON_UPDATE_INSTALLED;
    return HELIKON_OK;
}

int helikon_update_install(struct helikon_update *update)
{
    if (update == NULL)
        return HELIKON_ERR_ARGUMENT;

    int rc = HELIKON_OK;
    if (update->stage == HELIKON_UPDATE_IDLE)
        rc = helikon_update_verify(update);
    if (rc != HELIKON_OK)
        return rc;
    rc = helikon_update_park(update);
    if (rc != HELIKON_OK)
        return rc;
    return helikon_update_commit(update);
}
