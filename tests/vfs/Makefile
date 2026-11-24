# SPDX-License-Identifier: GPL-2.0-or-later
CC ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -std=c99 -Wall -Wextra -Werror -Wconversion -Wshadow -DHELIKON_HOST_TEST \
          $(EXTRA_CFLAGS)
CPPFLAGS += -I../../firmware/vfs -I../../firmware/hfs -I../hfsplus \
            -I../../firmware/codec -I../../firmware/codec/flac \
            -I../../firmware/codec/mp3 -I../../firmware/codec/libmad \
            -I../../firmware/player -I../../firmware/storage \
            -I../../firmware/block -I../../firmware/db \
            -idirafter ../../firmware/lib
VFS = ../../firmware/vfs
HFS = ../../firmware/hfs
TH = ../hfsplus
CODEC = ../../firmware/codec
LIB = ../../firmware/lib
PLAYER = ../../firmware/player
STORAGE = ../../firmware/storage
BLOCK = ../../firmware/block
DB = ../../firmware/db
TESTS = test_vfs test_vfs_write test_vfs_bookmark test_vfs_playlist \
        test_vfs_storage

# The VFS media adapter composes with the codec registry, whose MP3 descriptor
# needs the vendored libmad objects.
include ../libmad.mk

.PHONY: all test sanitize clean

all: $(TESTS)

test_vfs: test_vfs.c $(TH)/test_check.h \
          $(TH)/test_image.c $(TH)/test_image.h \
          $(TH)/test_fs_image.c $(TH)/test_fs_image.h \
          $(VFS)/vfs.c $(VFS)/vfs.h \
          $(HFS)/hfs_volume.c $(HFS)/hfs_volume.h \
          $(HFS)/hfs_catalog.c $(HFS)/hfs_catalog.h \
          $(HFS)/hfs_catalog_compare.c $(HFS)/hfs_catalog_compare.h \
          $(HFS)/hfs_extents.c $(HFS)/hfs_extents.h \
          $(HFS)/hfs_unicode.c $(HFS)/hfs_unicode.h \
          $(HFS)/hfs_unicode_data.c \
          $(HFS)/hfs_btree.c $(HFS)/hfs_btree.h \
          $(PLAYER)/vfs_media.c $(PLAYER)/vfs_media.h \
          $(CODEC)/codec.c $(CODEC)/codec.h \
          $(CODEC)/aiff/aiff_codec.c $(CODEC)/aiff/aiff_codec.h \
          $(CODEC)/flac/flac_codec.c $(CODEC)/flac/flac_decoder.c \
          $(CODEC)/mp3/mp3_codec.c $(CODEC)/mp3/mp3.h $(CODEC)/tag/id3v2.c \
          $(CODEC)/tag/id3v2.h $(PLAYER)/wav.c $(PLAYER)/wav.h $(LIBMAD_OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_vfs.c \
	  $(TH)/test_image.c $(TH)/test_fs_image.c \
	  $(VFS)/vfs.c \
	  $(HFS)/hfs_volume.c $(HFS)/hfs_catalog.c $(HFS)/hfs_catalog_compare.c \
	  $(HFS)/hfs_extents.c $(HFS)/hfs_unicode.c $(HFS)/hfs_unicode_data.c \
	  $(HFS)/hfs_btree.c \
	  $(PLAYER)/vfs_media.c \
	  $(CODEC)/codec.c $(CODEC)/aiff/aiff_codec.c $(CODEC)/flac/flac_codec.c \
	  $(CODEC)/flac/flac_decoder.c $(CODEC)/mp3/mp3_codec.c \
	  $(CODEC)/tag/id3v2.c \
	  $(PLAYER)/wav.c $(LIB)/mem.c $(LIBMAD_OBJ) $(LDFLAGS) -o $@

test: $(TESTS)
	./test_vfs
	./test_vfs_write
	./test_vfs_bookmark
	./test_vfs_playlist
	./test_vfs_storage

sanitize:
	$(MAKE) clean
	$(MAKE) EXTRA_CFLAGS="-O1 -fsanitize=address,undefined -fno-omit-frame-pointer"
	./test_vfs
	./test_vfs_write
	./test_vfs_bookmark
	./test_vfs_playlist
	./test_vfs_storage

test_vfs_write: test_vfs_write.c $(TH)/writer_fixture.c $(TH)/writer_fixture.h \
          $(TH)/test_check.h \
          $(VFS)/vfs.c $(VFS)/vfs.h $(VFS)/vfs_rw.c $(VFS)/vfs_rw.h \
          $(HFS)/hfs_volume.c $(HFS)/hfs_volume.h \
          $(HFS)/hfs_catalog.c $(HFS)/hfs_catalog.h \
          $(HFS)/hfs_catalog_write.c $(HFS)/hfs_catalog_write.h \
          $(HFS)/hfs_catalog_compare.c $(HFS)/hfs_catalog_compare.h \
          $(HFS)/hfs_btree.c $(HFS)/hfs_btree.h \
          $(HFS)/hfs_btree_write.c $(HFS)/hfs_btree_write.h \
          $(HFS)/hfs_btree_tree.c $(HFS)/hfs_btree_tree.h \
          $(HFS)/hfs_extents.c $(HFS)/hfs_extents.h \
          $(HFS)/hfs_extents_write.c $(HFS)/hfs_extents_write.h \
          $(HFS)/hfs_fork_io.c $(HFS)/hfs_fork_io.h \
          $(HFS)/hfs_bitmap.c $(HFS)/hfs_bitmap.h \
          $(HFS)/hfs_txn.c $(HFS)/hfs_txn.h \
          $(HFS)/hfs_journal.c $(HFS)/hfs_journal.h \
          $(HFS)/hfs_volume_write.c $(HFS)/hfs_volume_write.h \
          $(HFS)/hfs_check.c $(HFS)/hfs_check.h \
          $(HFS)/hfs_unicode.c $(HFS)/hfs_unicode.h \
          $(HFS)/hfs_unicode_data.c
	$(CC) $(CPPFLAGS) $(CFLAGS) test_vfs_write.c $(TH)/writer_fixture.c \
	  $(VFS)/vfs.c $(VFS)/vfs_rw.c \
	  $(HFS)/hfs_volume.c $(HFS)/hfs_catalog.c $(HFS)/hfs_catalog_write.c \
	  $(HFS)/hfs_catalog_compare.c $(HFS)/hfs_btree.c $(HFS)/hfs_btree_write.c \
	  $(HFS)/hfs_btree_tree.c $(HFS)/hfs_extents.c $(HFS)/hfs_extents_write.c \
	  $(HFS)/hfs_fork_io.c $(HFS)/hfs_bitmap.c $(HFS)/hfs_txn.c \
	  $(HFS)/hfs_journal.c $(HFS)/hfs_volume_write.c $(HFS)/hfs_check.c \
	  $(HFS)/hfs_unicode.c $(HFS)/hfs_unicode_data.c \
	  $(LIB)/mem.c $(LDFLAGS) -o $@

# The resume bookmark through the journaled VFS: text -> temp file -> rename.
test_vfs_bookmark: test_vfs_bookmark.c $(TH)/writer_fixture.c \
          $(TH)/writer_fixture.h $(TH)/test_check.h \
          $(VFS)/vfs.c $(VFS)/vfs.h $(VFS)/vfs_rw.c $(VFS)/vfs_rw.h \
          $(PLAYER)/bookmark.c $(PLAYER)/bookmark.h \
          $(PLAYER)/vfs_bookmark.c $(PLAYER)/vfs_bookmark.h \
          $(HFS)/hfs_volume.c $(HFS)/hfs_catalog.c $(HFS)/hfs_catalog_write.c \
          $(HFS)/hfs_catalog_compare.c $(HFS)/hfs_btree.c \
          $(HFS)/hfs_btree_write.c $(HFS)/hfs_btree_tree.c \
          $(HFS)/hfs_extents.c $(HFS)/hfs_extents_write.c \
          $(HFS)/hfs_fork_io.c $(HFS)/hfs_bitmap.c $(HFS)/hfs_txn.c \
          $(HFS)/hfs_journal.c $(HFS)/hfs_volume_write.c $(HFS)/hfs_check.c \
          $(HFS)/hfs_unicode.c $(HFS)/hfs_unicode_data.c $(LIB)/format.c
	$(CC) $(CPPFLAGS) $(CFLAGS) test_vfs_bookmark.c $(TH)/writer_fixture.c \
	  $(VFS)/vfs.c $(VFS)/vfs_rw.c \
	  $(PLAYER)/bookmark.c $(PLAYER)/vfs_bookmark.c $(LIB)/format.c \
	  $(HFS)/hfs_volume.c $(HFS)/hfs_catalog.c $(HFS)/hfs_catalog_write.c \
	  $(HFS)/hfs_catalog_compare.c $(HFS)/hfs_btree.c $(HFS)/hfs_btree_write.c \
	  $(HFS)/hfs_btree_tree.c $(HFS)/hfs_extents.c $(HFS)/hfs_extents_write.c \
	  $(HFS)/hfs_fork_io.c $(HFS)/hfs_bitmap.c $(HFS)/hfs_txn.c \
	  $(HFS)/hfs_journal.c $(HFS)/hfs_volume_write.c $(HFS)/hfs_check.c \
	  $(HFS)/hfs_unicode.c $(HFS)/hfs_unicode_data.c \
	  $(LIB)/mem.c $(LDFLAGS) -o $@

test_vfs_storage: test_vfs_storage.c $(TH)/writer_fixture.c \
          $(TH)/writer_fixture.h $(TH)/test_check.h \
          $(STORAGE)/mount.c $(STORAGE)/mount.h \
          $(BLOCK)/block.c $(BLOCK)/bounce.c $(BLOCK)/partition.c \
          $(HFS)/hfs_block_adapter.c \
          $(DB)/hdb.c $(DB)/hdb_vfs.c \
          $(VFS)/vfs.c $(VFS)/vfs.h $(VFS)/vfs_rw.c $(VFS)/vfs_rw.h \
          $(HFS)/hfs_volume.c $(HFS)/hfs_catalog.c $(HFS)/hfs_catalog_write.c \
          $(HFS)/hfs_catalog_compare.c $(HFS)/hfs_btree.c \
          $(HFS)/hfs_btree_write.c $(HFS)/hfs_btree_tree.c \
          $(HFS)/hfs_extents.c $(HFS)/hfs_extents_write.c \
          $(HFS)/hfs_fork_io.c $(HFS)/hfs_bitmap.c $(HFS)/hfs_txn.c \
          $(HFS)/hfs_journal.c $(HFS)/hfs_volume_write.c $(HFS)/hfs_check.c \
          $(HFS)/hfs_unicode.c $(HFS)/hfs_unicode_data.c $(LIB)/format.c
	$(CC) $(CPPFLAGS) $(CFLAGS) test_vfs_storage.c $(TH)/writer_fixture.c \
	  $(STORAGE)/mount.c $(BLOCK)/block.c $(BLOCK)/bounce.c \
	  $(BLOCK)/partition.c $(HFS)/hfs_block_adapter.c \
	  $(DB)/hdb.c $(DB)/hdb_vfs.c \
	  $(VFS)/vfs.c $(VFS)/vfs_rw.c \
	  $(LIB)/format.c \
	  $(HFS)/hfs_volume.c $(HFS)/hfs_catalog.c $(HFS)/hfs_catalog_write.c \
	  $(HFS)/hfs_catalog_compare.c $(HFS)/hfs_btree.c $(HFS)/hfs_btree_write.c \
	  $(HFS)/hfs_btree_tree.c $(HFS)/hfs_extents.c $(HFS)/hfs_extents_write.c \
	  $(HFS)/hfs_fork_io.c $(HFS)/hfs_bitmap.c $(HFS)/hfs_txn.c \
	  $(HFS)/hfs_journal.c $(HFS)/hfs_volume_write.c $(HFS)/hfs_check.c \
	  $(HFS)/hfs_unicode.c $(HFS)/hfs_unicode_data.c \
	  $(LIB)/crc32.c $(LIB)/mem.c $(LDFLAGS) -o $@

clean:
	rm -f $(TESTS)
	rm -rf *.dSYM build_mad

# Playlist export through the write VFS: text -> journaled file -> read VFS.
test_vfs_playlist: test_vfs_playlist.c $(TH)/writer_fixture.c \
          $(TH)/writer_fixture.h $(TH)/test_check.h \
          $(VFS)/vfs.c $(VFS)/vfs.h $(VFS)/vfs_rw.c $(VFS)/vfs_rw.h \
          $(PLAYER)/playlist.c $(PLAYER)/playlist.h \
          $(PLAYER)/vfs_playlist.c $(PLAYER)/vfs_playlist.h \
          $(HFS)/hfs_volume.c $(HFS)/hfs_catalog.c $(HFS)/hfs_catalog_write.c \
          $(HFS)/hfs_catalog_compare.c $(HFS)/hfs_btree.c \
          $(HFS)/hfs_btree_write.c $(HFS)/hfs_btree_tree.c \
          $(HFS)/hfs_extents.c $(HFS)/hfs_extents_write.c \
          $(HFS)/hfs_fork_io.c $(HFS)/hfs_bitmap.c $(HFS)/hfs_txn.c \
          $(HFS)/hfs_journal.c $(HFS)/hfs_volume_write.c $(HFS)/hfs_check.c \
          $(HFS)/hfs_unicode.c $(HFS)/hfs_unicode_data.c $(LIB)/format.c
	$(CC) $(CPPFLAGS) $(CFLAGS) test_vfs_playlist.c \
	  $(TH)/writer_fixture.c $(VFS)/vfs.c $(VFS)/vfs_rw.c \
	  $(PLAYER)/playlist.c $(PLAYER)/vfs_playlist.c $(LIB)/format.c \
	  $(HFS)/hfs_volume.c $(HFS)/hfs_catalog.c $(HFS)/hfs_catalog_write.c \
	  $(HFS)/hfs_catalog_compare.c $(HFS)/hfs_btree.c $(HFS)/hfs_btree_write.c \
	  $(HFS)/hfs_btree_tree.c $(HFS)/hfs_extents.c $(HFS)/hfs_extents_write.c \
	  $(HFS)/hfs_fork_io.c $(HFS)/hfs_bitmap.c $(HFS)/hfs_txn.c \
	  $(HFS)/hfs_journal.c $(HFS)/hfs_volume_write.c $(HFS)/hfs_check.c \
	  $(HFS)/hfs_unicode.c $(HFS)/hfs_unicode_data.c \
	  $(LIB)/mem.c $(LDFLAGS) -o $@
