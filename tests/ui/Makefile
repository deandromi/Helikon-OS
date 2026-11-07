# SPDX-License-Identifier: GPL-2.0-or-later
# Host renders of the launcher and the music browser.

CC ?= cc
CFLAGS := -O2 -g -std=c99 -Wall -Wextra -Werror -Wconversion -Wshadow \
          -Wundef -Wpointer-arith \
          -I../../firmware/ui -I../../firmware/diag -I../../firmware/lib \
          -I../../firmware/db -I../../firmware/hal/6g -I../../firmware/player
OUT := ../../build/ui
FIXTURE := ../../build/musicdb/fixture.hdb
BLOB_SRC := $(OUT)/fixture_blob.S
BLOB_OBJ := $(OUT)/fixture_blob.o
MUSIC_TEST := $(OUT)/test_music
AUDIO_TEST := $(OUT)/test_audio

.PHONY: test preview clean

test: preview

preview: test_launcher $(MUSIC_TEST) $(AUDIO_TEST) $(FIXTURE)
	@mkdir -p $(OUT)
	./test_launcher $(OUT)/launcher.ppm
	$(MUSIC_TEST) $(OUT)/music
	$(AUDIO_TEST) $(OUT)/audio
	python3 -c "from PIL import Image; \
[Image.open('$(OUT)/launcher.ppm').save('$(OUT)/launcher.png'), \
 *[Image.open('$(OUT)/music-%s.ppm' % n).save('$(OUT)/music-%s.png' % n) \
   for n in ('hub','albums','artists','tracks','artist','artist-albums','artist-tracks','album','track')], \
 *[Image.open('$(OUT)/audio-%s.ppm' % n).save('$(OUT)/audio-%s.png' % n) \
   for n in ('now-playing','paused')]]"
	@echo "previews: $(OUT)/launcher.png, music-*.png and audio-*.png"

test_launcher: test_launcher.c ../../firmware/ui/launcher.c \
                ../../firmware/ui/launcher_assets.c ../../firmware/ui/launcher.h \
                ../../firmware/ui/launcher_assets.h ../../firmware/diag/screen.c
	$(CC) $(CFLAGS) -o $@ test_launcher.c ../../firmware/ui/launcher.c \
	      ../../firmware/ui/launcher_assets.c ../../firmware/diag/screen.c

$(FIXTURE): ../../tools/hdb_build.py
	@mkdir -p $(dir $(FIXTURE))
	python3 ../../tools/hdb_build.py fixture --out $@

$(BLOB_SRC): $(FIXTURE)
	@mkdir -p $(OUT)
	@printf '.section __TEXT,__const\n.globl _helikon_hdb_start\n_helikon_hdb_start:\n.incbin "%s"\n.globl _helikon_hdb_end\n_helikon_hdb_end:\n' "$(abspath $(FIXTURE))" > $@

$(BLOB_OBJ): $(BLOB_SRC)
	$(CC) -c $< -o $@

$(MUSIC_TEST): test_music.c ../../firmware/ui/music.c ../../firmware/ui/music.h \
               ../../firmware/ui/launcher_assets.c \
               ../../firmware/ui/launcher_assets.h \
               ../../firmware/db/hdb.c ../../firmware/db/hdb.h \
               ../../firmware/diag/screen.c ../../firmware/lib/crc32.c \
               ../../firmware/lib/mem.c ../../firmware/lib/string.c \
               $(BLOB_OBJ)
	$(CC) $(CFLAGS) -o $@ test_music.c \
	      ../../firmware/ui/launcher_assets.c \
	      ../../firmware/db/hdb.c ../../firmware/diag/screen.c \
	      ../../firmware/lib/crc32.c ../../firmware/lib/mem.c \
	      ../../firmware/lib/string.c $(BLOB_OBJ)

$(AUDIO_TEST): test_audio.c ../../firmware/ui/music.c \
               ../../firmware/ui/music.h ../../firmware/player/player.c \
               ../../firmware/player/player.h ../../firmware/player/wav.c \
               ../../firmware/player/wav.h ../../firmware/hal/6g/audio.h \
               ../../firmware/ui/launcher_assets.c \
               ../../firmware/ui/launcher_assets.h \
               ../../firmware/db/hdb.c ../../firmware/db/hdb.h \
               ../../firmware/diag/screen.c ../../firmware/lib/crc32.c \
               ../../firmware/lib/mem.c ../../firmware/lib/string.c \
               $(BLOB_OBJ)
	$(CC) $(CFLAGS) -o $@ test_audio.c \
	      ../../firmware/player/player.c ../../firmware/player/wav.c \
	      ../../firmware/ui/launcher_assets.c \
	      ../../firmware/db/hdb.c ../../firmware/diag/screen.c \
	      ../../firmware/lib/crc32.c ../../firmware/lib/mem.c \
	      ../../firmware/lib/string.c $(BLOB_OBJ)

clean:
	rm -f test_launcher
