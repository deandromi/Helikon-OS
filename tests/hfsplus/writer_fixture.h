/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The writer fixture: one 1 MiB volume the catalog writer can create a file in,
 * built by hand so a test can put exactly one thing wrong. Shared by
 * `test_writer_mutations.c` (the curated and random mutation sweep) and the
 * writer fuzzer (`fuzz/fuzz_writer.c`), because a fuzzer needs the same base
 * image: a volume has to be valid before a mutation is interesting.
 *
 * The image is deliberately small and its geometry fixed: 512-byte blocks, 4 KiB
 * B-tree nodes, the catalog at block 16, the extents tree at 528 and the
 * attributes tree at 656, with the root folder (CNID 2) seeded through the real
 * writer so its records, thread record included, are ones the writer made.
 */
#ifndef HELIKON_TESTS_WRITER_FIXTURE_H
#define HELIKON_TESTS_WRITER_FIXTURE_H

#include "hfs_txn.h"
#include "hfs_volume.h"

#include <stddef.h>
#include <stdint.h>

#define WRITER_IMAGE_SIZE (1024u * 1024u)
#define WRITER_BLOCK_SIZE 512u
#define WRITER_NODE_SIZE 4096u
#define WRITER_BLOCKS (WRITER_IMAGE_SIZE / WRITER_BLOCK_SIZE)

#define WRITER_BITMAP_BLOCK 3u
#define WRITER_CATALOG_BLOCK 16u
#define WRITER_CATALOG_NODES 64u
#define WRITER_EXTENTS_BLOCK 528u
#define WRITER_EXTENTS_NODES 16u
#define WRITER_ATTRIBUTES_BLOCK 656u
#define WRITER_ATTRIBUTES_NODES 8u

/* A 64 KiB journal in the free span between the attributes tree and the
 * alternate header: the volume the write-VFS test commits through. */
#define WRITER_JOURNAL_BLOCK 832u
#define WRITER_JOURNAL_BLOCKS 128u
#define WRITER_JOURNAL_INFO_BLOCK 960u
#define WRITER_JOURNAL_SIZE (WRITER_JOURNAL_BLOCKS * WRITER_BLOCK_SIZE)
#define WRITER_JOURNAL_HEADER_SIZE 512u
#define WRITER_JOURNAL_BLOCK_LIST 4096u

#define WRITER_VOLUME_HEADER 1024u
#define WRITER_ALT_VOLUME_HEADER (WRITER_IMAGE_SIZE - 1024u)
#define WRITER_CATALOG_HEADER_NODE (WRITER_CATALOG_BLOCK * WRITER_BLOCK_SIZE)
#define WRITER_CATALOG_FIRST_LEAF (WRITER_CATALOG_HEADER_NODE + WRITER_NODE_SIZE)
#define WRITER_ROOT_CNID 2u

/* The image the fixture builds and every caller writes into. */
uint8_t *writer_fixture_image(void);

/* The read callback the volume is probed with. */
int writer_fixture_read(void *context, uint64_t offset, void *buffer,
                        size_t length);

/* Build the image and seed the root folder: 0 on success. */
int writer_fixture_build(struct hfs_volume *volume);

/* Apply a committed transaction to the image, as the device would. */
void writer_fixture_apply(const struct hfs_txn *tx);

/* The write callback the journal and the transaction engine write through. */
int writer_fixture_write(void *context, uint64_t offset, const void *buffer,
                         size_t length);
/* There is no cache in front of the image, so flushing is a successful no-op. */
int writer_fixture_flush(void *context);

/*
 * The same volume with a clean in-FS journal, so a transaction can commit the
 * way it does on a device: append, flush, replay. Without it the fixture is a
 * writer fixture, not a volume `vfs_rw` can open.
 */
int writer_fixture_build_journaled(struct hfs_volume *volume);

#endif
