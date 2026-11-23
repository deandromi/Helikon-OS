/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Where a resume bookmark lives (T-093).
 *
 * `bookmark.c` knows the record; this is the only code that knows the file. It
 * writes through the journaled `vfs_rw` and reads through the read-only `vfs`,
 * so a bookmark is a normal HFS+ file that `fsck_hfs` has an opinion about.
 *
 * The write is a replace, not an append: the record goes to `<path>.tmp`, is
 * read back, and only then replaces `<path>`. A power cut before the rename
 * leaves the previous bookmark intact and a temporary file nobody reads; a
 * power cut after it leaves the new one. Reading the record back before the
 * rename is the point of a bookmark: a resume that lands at a wrong second is
 * worse than one that does not happen.
 */
#ifndef HELIKON_PLAYER_VFS_BOOKMARK_H
#define HELIKON_PLAYER_VFS_BOOKMARK_H

#include "bookmark.h"
#include "vfs.h"
#include "vfs_rw.h"

/* Helikon owns `/.helikon/` (D-07); the bookmark is one file in it. */
#define HELIKON_BOOKMARK_DIR "/.helikon"
#define HELIKON_BOOKMARK_FILE "/.helikon/resume.txt"

/*
 * Writes `bookmark` to `path` through `rw`, replacing any previous record.
 * Returns HELIKON_OK, a taxonomy error, or HELIKON_ERR_IO when the bytes read
 * back are not the bytes written. Refuses a read-only writer, an invalid path
 * for the record, and a `path` with no room for its temporary sibling.
 */
int helikon_vfs_bookmark_save(struct vfs_rw *rw, const char *path,
                              const struct helikon_bookmark *bookmark);

/*
 * Reads `path` through `vfs` and parses it. A missing file is
 * HELIKON_ERR_NOT_FOUND (the normal state: nothing to resume), an oversized or
 * malformed one is HELIKON_ERR_FORMAT.
 */
int helikon_vfs_bookmark_load(struct vfs *vfs, const char *path,
                              struct helikon_bookmark *bookmark);

#endif
