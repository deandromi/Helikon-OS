# SPDX-License-Identifier: GPL-2.0-or-later
CC ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -std=c99 -Wall -Wextra -Werror -Wconversion -Wshadow
CPPFLAGS += -I../../firmware/hfs -I../../bootloader
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

.PHONY: all test crash-test write-diag-test outer-diag-test clean

all: hfs_image_test hfs_crash_test write_diag_host outer_diag_host loader_host

hfs_image_test: hfs_image_test.c $(COMMON) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) hfs_image_test.c $(COMMON) $(LDFLAGS) -o $@

hfs_crash_test: hfs_crash_test.c $(COMMON) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) hfs_crash_test.c $(COMMON) $(LDFLAGS) -o $@

write_diag_host: write_diag_host.c ../../bootloader/write_diag.c \
                 ../../bootloader/write_diag.h ../../bootloader/inner_image.c \
                 ../../bootloader/inner_image.h $(COMMON) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) write_diag_host.c \
	  ../../bootloader/write_diag.c ../../bootloader/inner_image.c \
	  $(COMMON) $(LDFLAGS) -o $@

outer_diag_host: outer_diag_host.c ../../bootloader/outer_diag.c \
                 ../../bootloader/outer_diag.h $(COMMON) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) outer_diag_host.c \
	  ../../bootloader/outer_diag.c $(COMMON) $(LDFLAGS) -o $@

loader_host: loader_host.c ../../bootloader/firmware_loader.c \
             ../../bootloader/firmware_loader.h ../../bootloader/hfs_loader.c \
             ../../bootloader/hfs_loader.h ../../bootloader/manifest.c \
             ../../bootloader/manifest.h ../../bootloader/bootstate.c \
             ../../bootloader/bootstate.h ../../bootloader/sha256.c \
             ../../bootloader/sha256.h ../../bootloader/loader_error.c \
             ../../bootloader/loader_error.h $(COMMON) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) loader_host.c \
	  ../../bootloader/firmware_loader.c ../../bootloader/hfs_loader.c \
	  ../../bootloader/manifest.c ../../bootloader/bootstate.c \
	  ../../bootloader/sha256.c ../../bootloader/loader_error.c \
	  $(COMMON) $(LDFLAGS) -o $@

# The shell drivers create their own disposable images. Never point them at a
# library or a mounted device.
test: hfs_image_test
	./run-macos.sh

crash-test: hfs_crash_test
	./run-crash-macos.sh

write-diag-test: write_diag_host
	./run-write-diag-macos.sh

outer-diag-test: outer_diag_host
	./run-outer-diag-macos.sh

loader-test: loader_host
	./run-loader-macos.sh

clean:
	rm -rf hfs_image_test hfs_crash_test write_diag_host outer_diag_host loader_host *.dSYM
