/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HELIKON_HFS_VOLUME_WRITE_H
#define HELIKON_HFS_VOLUME_WRITE_H

#include "hfs_txn.h"
#include "hfs_volume.h"

#include <stdint.h>

struct hfs_volume_update {
    int set_file_count;
    uint32_t file_count;
    int set_folder_count;
    uint32_t folder_count;
    int set_next_cnid;
    uint32_t next_cnid;
};

/*
 * Stage the primary and alternate volume headers with the requested counts and
 * an incremented write count. The caller commits through hfs_txn.
 */
int hfs_volume_stage(struct hfs_txn *tx, const struct hfs_volume *volume,
                     const struct hfs_volume_update *update);

#endif
