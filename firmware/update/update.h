/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The signed staged-update client (T-064).
 *
 * The loader reads `/.helikon/firmware.manifest`, `/.helikon/firmware.img` and
 * `/.helikon/bootstate` and refuses to jump unless the manifest verifies, the
 * image matches it and the boot state agrees with both (bootloader/LOADER.md).
 * This file is the other half: the firmware-side path that turns a staged set
 * under `/.helikon/update/` into that installed triple, without ever leaving a
 * manifest that disagrees with its image.
 *
 * Three phases, in the order a caller runs them:
 *
 *   1. `helikon_update_verify` — the staged pair is checked *before* anything is
 *      written: the manifest's magic, version, board, load window, size, key ID,
 *      flags and Ed25519 signature (through the loader's own verifier, not a
 *      copy), then the image's exact length and SHA-256, streamed a chunk at a
 *      time so an 80 KiB image does not need 80 KiB of RAM. A set for another
 *      board, an unsigned set, a development set on a release build or an image
 *      that does not hash to what the manifest says is refused here, with
 *      nothing on the volume touched.
 *
 *   2. `helikon_update_park` — the *pipeline* half. It copies the installed pair
 *      into `/.helikon/recovery/` (the manual restore copy, D-31) and copies the
 *      staged image to `/.helikon/update/firmware.img.ready`, verifying both by
 *      reading them back and re-hashing. After park the installed set is
 *      untouched and still bootable: a power cut here costs nothing.
 *
 *   3. `helikon_update_commit` — the single transaction. In one commit it
 *      removes the old image, renames the parked one into place, rewrites the
 *      manifest and rewrites the boot state with a TRYING slot for the new
 *      facts. Two files cannot be swapped atomically; one transaction can, and
 *      that is the whole reason the VFS grew a transaction scope. A cut before
 *      the commit leaves the old set, a cut after it leaves the new one, and
 *      there is no state in between where the manifest describes an image that
 *      is not there.
 *
 * What this file deliberately does not do: it never deletes the staged set (a
 * host wrote it; the firmware does not remove files it did not create), it never
 * repairs or reformats anything, and it never falls back to "install anyway".
 * Every refusal leaves the volume as it was.
 *
 * The boot state is part of the commit, not an afterthought. The loader
 * validates the selected GOOD slot against the manifest it verified, so a GOOD
 * slot still describing the replaced image would turn every boot after the
 * update into a recovery screen. `helikon_bootstate_stage_trying` demotes the
 * old GOOD slot and stages the new facts as TRYING in the same buffer, and
 * `helikon_bootstate_promote` (T-055) promotes it once the new image passes its
 * own self-test.
 */
#ifndef HELIKON_UPDATE_H
#define HELIKON_UPDATE_H

#include <stddef.h>
#include <stdint.h>

#include "keys.h"
#include "manifest.h"
#include "vfs.h"
#include "vfs_rw.h"

/* One read or write unit for the streaming copy. */
#define HELIKON_UPDATE_CHUNK 4096u
#define HELIKON_UPDATE_MANIFEST_BYTES 256u
#define HELIKON_UPDATE_BOOTSTATE_BYTES 512u

/* The loader's fixed layout, plus the two directories an update needs. */
#define HELIKON_UPDATE_ROOT "/.helikon"
#define HELIKON_UPDATE_STAGING "/.helikon/update"
#define HELIKON_UPDATE_RECOVERY "/.helikon/recovery"
#define HELIKON_UPDATE_STAGED_MANIFEST "/.helikon/update/firmware.manifest"
#define HELIKON_UPDATE_STAGED_IMAGE "/.helikon/update/firmware.img"
#define HELIKON_UPDATE_PARKED_IMAGE "/.helikon/update/firmware.img.ready"
#define HELIKON_UPDATE_INSTALLED_MANIFEST "/.helikon/firmware.manifest"
#define HELIKON_UPDATE_INSTALLED_IMAGE "/.helikon/firmware.img"
#define HELIKON_UPDATE_RECOVERY_MANIFEST "/.helikon/recovery/firmware.manifest"
#define HELIKON_UPDATE_RECOVERY_IMAGE "/.helikon/recovery/firmware.img"
#define HELIKON_UPDATE_BOOTSTATE "/.helikon/bootstate"

enum helikon_update_stage {
    HELIKON_UPDATE_IDLE = 0,
    HELIKON_UPDATE_VERIFIED = 1,  /* the staged set may be installed */
    HELIKON_UPDATE_PARKED = 2,    /* the recovery copy and the parked image exist */
    HELIKON_UPDATE_INSTALLED = 3  /* the one-commit swap happened */
};

struct helikon_update {
    struct vfs *vfs;                  /* mounted read-only or read-write */
    struct vfs_rw *rw;                /* NULL: verify only, no install */
    const struct helikon_key_table *keys;
    uint32_t board;
    int allow_development;
    uint8_t chunk[HELIKON_UPDATE_CHUNK];
    uint8_t manifest_bytes[HELIKON_UPDATE_MANIFEST_BYTES];
    struct helikon_manifest staged;   /* what the staged manifest says */
    struct helikon_manifest installed; /* what is installed now, if it parses */
    uint8_t installed_bytes[HELIKON_UPDATE_MANIFEST_BYTES];
    int have_installed;               /* the installed manifest parsed */
    uint32_t bootstate_sequence;      /* filled by commit */
    uint8_t stage;                    /* enum helikon_update_stage */
};

/*
 * Bind the object to a mounted VFS and, if the volume may be written, to a
 * writable VFS. `keys` is the loader's key table; a NULL table cannot verify
 * anything and `verify` will refuse every set. `board` is the board this
 * firmware runs on (`HELIKON_BOARD_IPOD6G`); `allow_development` permits a
 * development-only manifest, as the diagnostic loader does.
 *
 * Nothing is read here: the first call that touches the volume is `verify`.
 */
int helikon_update_open(struct helikon_update *update, struct vfs *vfs,
                        struct vfs_rw *rw,
                        const struct helikon_key_table *keys, uint32_t board,
                        int allow_development);

/*
 * Check the staged pair. On success `update->staged` holds the verified
 * manifest and the stage is HELIKON_UPDATE_VERIFIED. Refusals:
 *
 *   - NOT_FOUND: no staged manifest or no staged image;
 *   - FORMAT: the manifest is not a Helikon manifest, or the image is not the
 *     length or the hash the manifest signs;
 *   - UNSUPPORTED: another board, an unknown key id, a bad signature, or a
 *     development-only set on a build that does not allow one;
 *   - ARGUMENT: the staged image version is not higher than the installed one
 *     (a device that can be downgraded is a device that can be rolled back onto
 *     a bug), or the installed manifest is present but does not verify.
 */
int helikon_update_verify(struct helikon_update *update);

/*
 * Copy the installed pair into `/.helikon/recovery/` and park the staged image
 * beside the installed one, verifying both by reading them back. The installed
 * set is untouched. Requires a writable VFS (READ_ONLY otherwise) and a
 * verified set (ARGUMENT otherwise).
 */
int helikon_update_park(struct helikon_update *update);

/*
 * The single commit: remove the installed image, move the parked image into its
 * place, rewrite the manifest and the boot state, flush, and read all three back
 * byte for byte. A failure at any point leaves the old set in place and answers
 * with the reason; a failure of the read-back means the commit landed but the
 * medium does not agree, which is IO and stops the update there.
 */
int helikon_update_commit(struct helikon_update *update);

/* verify (if needed), park, commit. */
int helikon_update_install(struct helikon_update *update);

#endif
