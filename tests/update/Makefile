# SPDX-License-Identifier: GPL-2.0-or-later
#
# The signed staged-update client (T-064): a journaled in-memory volume, really
# signed fixture sets, and the loader's own verifier rather than a copy of it.
CC ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -std=c99 -Wall -Wextra -Werror -Wconversion -Wshadow \
          -DHELIKON_HOST_TEST $(EXTRA_CFLAGS)
CPPFLAGS += -I. -I../../firmware/update -I../../firmware/vfs \
            -I../../firmware/hfs -I../../firmware/boot -I../../bootloader \
            -I../hfsplus -idirafter ../../firmware/lib

UPDATE = ../../firmware/update
VFS = ../../firmware/vfs
HFS = ../../firmware/hfs
BOOT = ../../firmware/boot
LOADER = ../../bootloader
TH = ../hfsplus
LIB = ../../firmware/lib

# The loader's verifiers, shared rather than copied: one implementation, one
# opinion about what a signed set is.
VERIFY = $(LOADER)/manifest.c $(LOADER)/manifest.h \
         $(LOADER)/keys.c $(LOADER)/keys.h \
         $(LOADER)/ed25519.c $(LOADER)/ed25519.h \
         $(LOADER)/loader_error.c $(LOADER)/loader_error.h

HFS_SRC = $(HFS)/hfs_catalog.c $(HFS)/hfs_catalog_compare.c \
          $(HFS)/hfs_catalog_write.c $(HFS)/hfs_unicode.c \
          $(HFS)/hfs_unicode_data.c $(HFS)/hfs_btree.c \
          $(HFS)/hfs_btree_write.c $(HFS)/hfs_btree_tree.c \
          $(HFS)/hfs_fork_io.c $(HFS)/hfs_volume.c $(HFS)/hfs_volume_write.c \
          $(HFS)/hfs_bitmap.c $(HFS)/hfs_extents.c $(HFS)/hfs_extents_write.c \
          $(HFS)/hfs_txn.c $(HFS)/hfs_journal.c $(HFS)/hfs_check.c

.PHONY: all test sanitize fixtures-check clean

all: test_update

test_update: test_update.c update_test_key.h $(UPDATE)/update.c \
             $(UPDATE)/update.h $(VFS)/vfs.c $(VFS)/vfs.h $(VFS)/vfs_rw.c \
             $(VFS)/vfs_rw.h $(BOOT)/bootstate.c $(BOOT)/bootstate.h \
             $(VERIFY) $(LIB)/sha256.c $(LIB)/sha256.h $(LIB)/mem.c \
             $(TH)/writer_fixture.c $(TH)/writer_fixture.h $(HFS_SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) test_update.c \
	  $(UPDATE)/update.c $(VFS)/vfs.c $(VFS)/vfs_rw.c \
	  $(BOOT)/bootstate.c $(LOADER)/manifest.c $(LOADER)/keys.c \
	  $(LOADER)/ed25519.c $(LOADER)/loader_error.c \
	  $(LIB)/sha256.c $(LIB)/mem.c $(TH)/writer_fixture.c \
	  $(HFS_SRC) $(LDFLAGS) -o $@

test: test_update
	./test_update

sanitize:
	$(MAKE) clean
	$(MAKE) EXTRA_CFLAGS="-O1 -fsanitize=address,undefined -fno-omit-frame-pointer"
	./test_update

# The fixture sets are signed bytes; re-generating them must be a no-op.
fixtures-check:
	python3 make_update_set.py --check

clean:
	rm -rf test_update *.dSYM
