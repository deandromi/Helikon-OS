/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The Music hub's boot path. See `music_boot.h` for the three rules; this file
 * is only the order they run in.
 */
#include "music_boot.h"

#include <stddef.h>

#include "helikon_error.h"

int helikon_music_boot_open(struct helikon_music_boot *boot,
                            const struct helikon_block_device *device,
                            uint32_t timeout_ms)
{
    if (boot == NULL || device == NULL)
        return HELIKON_ERR_ARGUMENT;

    boot->db = NULL;
    boot->error = HELIKON_OK;
    boot->writable = 0u;

    int rc = helikon_storage_mount(&boot->storage, device, timeout_ms);
    if (rc != HELIKON_OK) {
        boot->error = rc;
        return rc;
    }

    /*
     * Reading does not depend on writing: a volume somebody deliberately left
     * read-only, or one with no journal, is still a library. The answer is kept
     * so a settings screen can say why saving is unavailable.
     */
    int gate = helikon_storage_enable_write(&boot->storage);
    boot->writable = boot->storage.writable;

    rc = helikon_storage_open_catalog(&boot->storage);
    if (rc != HELIKON_OK) {
        /* Rule 1: a mount with no usable catalog is not left standing. */
        helikon_storage_unmount(&boot->storage);
        boot->writable = 0u;
        boot->error = rc == HELIKON_OK ? HELIKON_ERR_NOT_FOUND : rc;
        return boot->error;
    }

    boot->db = &boot->storage.db;
    /*
     * A refused write gate is reported through `writable`, not through the
     * return code: the library opened, which is what the caller asked for. The
     * gate's own answer is worth keeping, though, and HELIKON_ERR_NOT_FOUND is
     * its normal "this volume did not opt in" case rather than a fault.
     */
    if (gate != HELIKON_OK && gate != HELIKON_ERR_NOT_FOUND)
        boot->error = gate;
    return HELIKON_OK;
}

int helikon_music_boot_from_ata(struct helikon_music_boot *boot,
                                struct helikon_ata *ata, int writable,
                                uint32_t timeout_ms)
{
    if (boot == NULL || ata == NULL)
        return HELIKON_ERR_ARGUMENT;

    boot->db = NULL;
    boot->error = HELIKON_OK;
    boot->writable = 0u;

    int rc = helikon_ata_block_init(&boot->ata_block, ata, writable);
    if (rc != HELIKON_OK) {
        /* No IDENTIFY, or geometry that does not add up: there is no device to
         * mount, and saying so is better than mounting a guess. */
        boot->error = rc;
        return rc;
    }

    return helikon_music_boot_open(
        boot, helikon_ata_block_device(&boot->ata_block), timeout_ms);
}

void helikon_music_boot_close(struct helikon_music_boot *boot)
{
    if (boot == NULL)
        return;
    helikon_storage_unmount(&boot->storage);
    boot->db = NULL;
    boot->writable = 0u;
    boot->error = HELIKON_OK;
}
