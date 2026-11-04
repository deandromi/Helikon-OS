# SPDX-License-Identifier: GPL-2.0-or-later
CC ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -std=c99 -Wall -Wextra -Werror -Wconversion -Wshadow
CPPFLAGS += -I../../firmware/hfs -I../../bootloader -I../hfsplus
HFS = ../../firmware/hfs
SOURCES = test_diag.c ../hfsplus/test_image.c ../hfsplus/test_fs_image.c \
          ../../bootloader/diagnostics.c ../../bootloader/report.c ../../bootloader/splash.c \
          $(HFS)/hfs_catalog.c $(HFS)/hfs_unicode.c $(HFS)/hfs_unicode_data.c \
          $(HFS)/hfs_extents.c \
          $(HFS)/hfs_btree.c $(HFS)/hfs_volume.c
HEADERS = ../../bootloader/diagnostics.h ../../bootloader/report.h ../../bootloader/splash.h \
          ../hfsplus/test_image.h ../hfsplus/test_fs_image.h \
          ../hfsplus/test_check.h $(HFS)/hfs_catalog.h $(HFS)/hfs_unicode.h \
          $(HFS)/hfs_extents.h $(HFS)/hfs_btree.h $(HFS)/hfs_volume.h

INNER_SOURCES = test_inner_image.c ../../bootloader/inner_image.c \
                $(HFS)/hfs_catalog.c $(HFS)/hfs_extents.c \
                $(HFS)/hfs_unicode.c $(HFS)/hfs_unicode_data.c \
                $(HFS)/hfs_btree.c $(HFS)/hfs_volume.c
INNER_HEADERS = ../../bootloader/inner_image.h \
                $(HFS)/hfs_catalog.h $(HFS)/hfs_extents.h \
                $(HFS)/hfs_unicode.h $(HFS)/hfs_btree.h $(HFS)/hfs_volume.h

LOADER_SOURCES = test_loader.c ../../bootloader/sha256.c \
                 ../../bootloader/manifest.c ../../bootloader/bootstate.c \
                 ../../bootloader/firmware_loader.c ../../bootloader/loader_error.c \
                 ../../bootloader/ed25519.c ../../bootloader/keys.c
LOADER_HEADERS = ../../bootloader/sha256.h ../../bootloader/manifest.h \
                 ../../bootloader/bootstate.h ../../bootloader/firmware_loader.h \
                 ../../bootloader/loader_error.h ../../bootloader/ed25519.h \
                 ../../bootloader/keys.h ../hfsplus/test_check.h

ED25519_SOURCES = test_ed25519.c ../../bootloader/ed25519.c \
                  ../../bootloader/keys.c
ED25519_HEADERS = ../../bootloader/ed25519.h ../../bootloader/keys.h \
                  ../hfsplus/test_check.h

HFS_LOADER_SOURCES = test_loader_hfs.c ../hfsplus/test_image.c \
                     ../hfsplus/test_fs_image.c ../../bootloader/hfs_loader.c \
                     ../../bootloader/loader_error.c $(HFS)/hfs_catalog.c \
                     $(HFS)/hfs_extents.c $(HFS)/hfs_unicode.c \
                     $(HFS)/hfs_unicode_data.c $(HFS)/hfs_btree.c \
                     $(HFS)/hfs_volume.c
HFS_LOADER_HEADERS = ../../bootloader/hfs_loader.h \
                     ../../bootloader/firmware_loader.h \
                     ../../bootloader/loader_error.h ../hfsplus/test_image.h \
                     ../hfsplus/test_fs_image.h ../hfsplus/test_check.h \
                     $(HFS)/hfs_catalog.h $(HFS)/hfs_extents.h \
                     $(HFS)/hfs_btree.h $(HFS)/hfs_volume.h

.PHONY: all test sanitize clean

all: test_diag test_inner_image test_loader test_loader_hfs test_ed25519

test_diag: $(SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) $(LDFLAGS) -o $@

test_inner_image: $(INNER_SOURCES) $(INNER_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(INNER_SOURCES) $(LDFLAGS) -o $@

test_loader: $(LOADER_SOURCES) $(LOADER_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LOADER_SOURCES) $(LDFLAGS) -o $@

test_loader_hfs: $(HFS_LOADER_SOURCES) $(HFS_LOADER_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HFS_LOADER_SOURCES) $(LDFLAGS) -o $@

test_ed25519: $(ED25519_SOURCES) $(ED25519_HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(ED25519_SOURCES) $(LDFLAGS) -o $@

test: test_diag test_inner_image test_loader test_loader_hfs test_ed25519
	./test_diag
	./test_inner_image
	./test_loader
	./test_loader_hfs
	./test_ed25519

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
SAN_CFLAGS = -O1 -g -std=c99 -Wall -Wextra -Werror -Wconversion -Wshadow \
             -fsanitize=address,undefined -fno-omit-frame-pointer
SAN_LDFLAGS = -fsanitize=address,undefined
else
SAN_CFLAGS = -O1 -g -std=c99 -Wall -Wextra -Werror -Wconversion -Wshadow \
             -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie
SAN_LDFLAGS = -fsanitize=address,undefined -no-pie
endif

sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS='$(SAN_CFLAGS)' LDFLAGS='$(SAN_LDFLAGS)' test

clean:
	rm -rf test_diag test_inner_image test_loader test_loader_hfs test_ed25519 *.dSYM
