# SPDX-License-Identifier: GPL-2.0-or-later
CC ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -std=c99 -Wall -Wextra -Werror -Wconversion -Wshadow
CPPFLAGS += -I../../firmware/hfs
HFS = ../../firmware/hfs

COMMON = $(HFS)/hfs_catalog.c $(HFS)/hfs_catalog_compare.c \
	 $(HFS)/hfs_catalog_write.c \
	 $(HFS)/hfs_unicode.c $(HFS)/hfs_unicode_data.c \
	 $(HFS)/hfs_btree_tree.c $(HFS)/hfs_btree_write.c \
	 $(HFS)/hfs_fork_io.c $(HFS)/hfs_volume_write.c \
	 $(HFS)/hfs_bitmap.c $(HFS)/hfs_extents.c \
	 $(HFS)/hfs_extents_write.c \
	 $(HFS)/hfs_txn.c $(HFS)/hfs_journal.c \
	 $(HFS)/hfs_btree.c $(HFS)/hfs_volume.c

HEADERS = $(HFS)/hfs_catalog.h $(HFS)/hfs_catalog_compare.h \
	  $(HFS)/hfs_catalog_write.h $(HFS)/hfs_unicode.h \
	  $(HFS)/hfs_btree_tree.h $(HFS)/hfs_btree_write.h \
	  $(HFS)/hfs_fork_io.h $(HFS)/hfs_volume_write.h \
	  $(HFS)/hfs_bitmap.h $(HFS)/hfs_extents.h \
	  $(HFS)/hfs_extents_write.h $(HFS)/hfs_txn.h \
	  $(HFS)/hfs_journal.h $(HFS)/hfs_btree.h $(HFS)/hfs_volume.h

.PHONY: all test crash-test clean

all: hfs_image_test hfs_crash_test

hfs_image_test: hfs_image_test.c $(COMMON) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) hfs_image_test.c $(COMMON) $(LDFLAGS) -o $@

hfs_crash_test: hfs_crash_test.c $(COMMON) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) hfs_crash_test.c $(COMMON) $(LDFLAGS) -o $@

# The shell drivers create their own disposable images. Never point them at a
# library or a mounted device.
test: hfs_image_test
	./run-macos.sh

crash-test: hfs_crash_test
	./run-crash-macos.sh

clean:
	rm -rf hfs_image_test hfs_crash_test *.dSYM
