# SPDX-License-Identifier: GPL-2.0-or-later
# Host test for HelikonDB and audio read from a real HFS+ volume through the
# read-only VFS. The image itself comes from `run-library-macos.sh` (hdiutil) or
# from any journaled HFS+ image that holds `/.helikon/music.hdb`, the files the
# catalog points at, `/Music/first.mp3` and `/Playlists/first.m3u` (the script
# copies `tests/codec/fixtures/mp3_cbr_stereo16.mp3` into each of them);
# `macos-sanitize` runs the same path under ASan and UBSan.

CC ?= cc
# `-idirafter` keeps the host libc headers for the HFS core (which expects
# memcmp/strlen from <string.h>) while letting the firmware headers resolve, the
# same arrangement tests/vfs uses.
CFLAGS := -O2 -g -std=c99 -Wall -Wextra -Werror -Wconversion -Wshadow \
          -Wundef -Wpointer-arith -DHELIKON_HOST_TEST \
          -I../../firmware/db -I../../firmware/vfs -I../../firmware/hfs \
          -I../../firmware/player -I../../firmware/library \
          -I../../firmware/storage -I../../firmware/block \
          -I../../firmware/codec \
          -I../../firmware/codec/flac -I../../firmware/codec/mp3 \
          -I../../firmware/codec/libmad -I../../firmware/hal/6g \
          -idirafter ../../firmware/lib

# The player pulls through the codec registry, whose MP3 descriptor needs the
# vendored libmad objects.
include ../libmad.mk

# `SAN=1` runs the same image test under the sanitizers; the macOS script
# forwards it.
SAN ?= 0
ifeq ($(SAN),1)
CFLAGS += -O1 -fsanitize=address,undefined -fno-omit-frame-pointer
endif

BIN := test_library
VFS := ../../firmware/vfs
HFS := ../../firmware/hfs
DB := ../../firmware/db
LIB := ../../firmware/lib

PLAYER := ../../firmware/player
CODEC := ../../firmware/codec
LIBRARY := ../../firmware/library
STORAGE := ../../firmware/storage
BLOCK := ../../firmware/block

SOURCES := test_library.c \
           $(VFS)/vfs.c \
           $(HFS)/hfs_volume.c $(HFS)/hfs_catalog.c \
           $(HFS)/hfs_catalog_compare.c $(HFS)/hfs_extents.c \
           $(HFS)/hfs_unicode.c $(HFS)/hfs_unicode_data.c \
           $(HFS)/hfs_btree.c \
           $(DB)/hdb.c $(DB)/hdb_vfs.c \
           $(PLAYER)/player.c $(PLAYER)/wav.c $(PLAYER)/vfs_media.c \
           $(PLAYER)/read_ahead.c $(PLAYER)/playlist.c $(PLAYER)/queue.c \
           $(LIBRARY)/library.c $(LIBRARY)/music_source.c \
           $(STORAGE)/mount.c $(BLOCK)/block.c $(BLOCK)/bounce.c \
           $(BLOCK)/partition.c $(HFS)/hfs_block_adapter.c \
           $(CODEC)/codec.c $(CODEC)/flac/flac_codec.c \
           $(CODEC)/flac/flac_decoder.c $(CODEC)/mp3/mp3_codec.c \
           $(CODEC)/tag/id3v2.c $(CODEC)/tag/vorbis.c \
           $(LIB)/crc32.c $(LIB)/mem.c $(LIB)/string.c $(LIBMAD_OBJ)

.PHONY: all test macos macos-sanitize sanitize clean

all: $(BIN)

$(BIN): $(SOURCES) $(DB)/hdb.h $(DB)/hdb_vfs.h $(VFS)/vfs.h \
        $(PLAYER)/player.h $(PLAYER)/read_ahead.h
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
	SAN=$(SAN) ./run-library-macos.sh

macos-sanitize:
	SAN=1 ./run-library-macos.sh

clean:
	rm -f $(BIN)
	rm -rf $(BIN).dSYM build_mad
