# SPDX-License-Identifier: GPL-2.0-or-later
# Host test for HelikonDB read from a real HFS+ volume through the read-only
# VFS. The image itself comes from `run-library-macos.sh` (hdiutil) or from any
# journaled HFS+ image that holds `/.helikon/music.hdb`.

CC ?= cc
# `-idirafter` keeps the host libc headers for the HFS core (which expects
# memcmp/strlen from <string.h>) while letting the firmware headers resolve, the
# same arrangement tests/vfs uses.
CFLAGS := -O2 -g -std=c99 -Wall -Wextra -Werror -Wconversion -Wshadow \
          -Wundef -Wpointer-arith \
          -I../../firmware/db -I../../firmware/vfs -I../../firmware/hfs \
          -idirafter ../../firmware/lib

BIN := test_library
VFS := ../../firmware/vfs
HFS := ../../firmware/hfs
DB := ../../firmware/db
LIB := ../../firmware/lib

SOURCES := test_library.c \
           $(VFS)/vfs.c \
           $(HFS)/hfs_volume.c $(HFS)/hfs_catalog.c \
           $(HFS)/hfs_catalog_compare.c $(HFS)/hfs_extents.c \
           $(HFS)/hfs_unicode.c $(HFS)/hfs_unicode_data.c \
           $(HFS)/hfs_btree.c \
           $(DB)/hdb.c $(DB)/hdb_vfs.c \
           $(LIB)/crc32.c $(LIB)/mem.c $(LIB)/string.c

.PHONY: all test macos sanitize clean

all: $(BIN)

$(BIN): $(SOURCES) $(DB)/hdb.h $(DB)/hdb_vfs.h $(VFS)/vfs.h
	$(CC) $(CFLAGS) -o $@ $(SOURCES)

test: $(BIN)
	@test -n "$(IMAGE)" || \
	  (echo "usage: make -C tests/library test IMAGE=/path/to/hfs-image"; \
	   exit 2)
	./$(BIN) "$(IMAGE)"

sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS="$(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer" test IMAGE="$(IMAGE)"

macos:
	./run-library-macos.sh

clean:
	rm -f $(BIN)
	rm -rf $(BIN).dSYM
