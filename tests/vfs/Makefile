# SPDX-License-Identifier: GPL-2.0-or-later
CC ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -std=c99 -Wall -Wextra -Werror -Wconversion -Wshadow
CPPFLAGS += -I../../firmware/vfs -I../../firmware/hfs -I../hfsplus -idirafter ../../firmware/lib
VFS = ../../firmware/vfs
HFS = ../../firmware/hfs
TH = ../hfsplus
TESTS = test_vfs

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
          $(HFS)/hfs_btree.c $(HFS)/hfs_btree.h
	$(CC) $(CPPFLAGS) $(CFLAGS) test_vfs.c \
	  $(TH)/test_image.c $(TH)/test_fs_image.c \
	  $(VFS)/vfs.c \
	  $(HFS)/hfs_volume.c $(HFS)/hfs_catalog.c $(HFS)/hfs_catalog_compare.c \
	  $(HFS)/hfs_extents.c $(HFS)/hfs_unicode.c $(HFS)/hfs_unicode_data.c \
	  $(HFS)/hfs_btree.c $(LDFLAGS) -o $@

test: $(TESTS)
	./test_vfs

sanitize: $(TESTS)
	$(MAKE) clean
	$(MAKE) CFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer"
	./test_vfs

clean:
	rm -f $(TESTS)
	rm -rf *.dSYM
