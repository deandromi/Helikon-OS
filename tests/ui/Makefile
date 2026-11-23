# SPDX-License-Identifier: GPL-2.0-or-later
# Host renders of the launcher and the music browser.

CC ?= cc
CFLAGS := -O2 -g -std=c99 -Wall -Wextra -Werror -Wconversion -Wshadow \
          -Wundef -Wpointer-arith \
          -I../../firmware/ui -I../../firmware/diag -I../../firmware/lib \
          -I../../firmware/db -I../../firmware/hal/6g -I../../firmware/player \
          -I../../firmware/codec -I../../firmware/codec/flac \
          -I../../firmware/codec/mp3 -I../../firmware/codec/libmad

# The audio render links the codec layer, whose registry references the MP3
# descriptor, so the libmad objects come along even for WAV-only renders.
include ../libmad.mk
OUT := ../../build/ui
FIXTURE := ../../build/musicdb/fixture.hdb
BLOB_SRC := $(OUT)/fixture_blob.S
BLOB_OBJ := $(OUT)/fixture_blob.o

# The HDB fixture is linked as an incbin blob. The assembler syntax differs:
# Mach-O wants a segment/section pair and an underscore on every symbol, ELF
# wants a plain section name and the symbol as written. CI builds on both.
ifeq ($(shell uname -s),Darwin)
BLOB_SECTION := __TEXT,__const
BLOB_SYMBOL := _
else
BLOB_SECTION := .rodata
BLOB_SYMBOL :=
endif
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
   for n in ('hub','albums','tracks','artists','artists-long','artists-empty', \
             'artist-albums','artist-albums-empty','artist-overview', \
             'artist-overview-empty','artist-tracks','artist-tab-focus', \
             'album','album-long', \
             'album-playall','album-empty','album-corrupt','album-no-art', \
             'artists-fallback')], \
 *[Image.open('$(OUT)/audio-%s.ppm' % n).save('$(OUT)/audio-%s.png' % n) \
   for n in ('now-playing','paused','reduced','viz-off','opening', \
             'finished','error','error-terminal','no-art','long-text')]]"
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
	@{ \
	    echo ".section $(BLOB_SECTION)"; \
	    echo ".globl $(BLOB_SYMBOL)helikon_hdb_start"; \
	    echo "$(BLOB_SYMBOL)helikon_hdb_start:"; \
	    echo ".incbin \"$(abspath $(FIXTURE))\""; \
	    echo ".globl $(BLOB_SYMBOL)helikon_hdb_end"; \
	    echo "$(BLOB_SYMBOL)helikon_hdb_end:"; \
	} > $@

$(BLOB_OBJ): $(BLOB_SRC)
	$(CC) -c $< -o $@

$(MUSIC_TEST): test_music.c ../../firmware/ui/music.c ../../firmware/ui/music.h \
               ../../firmware/ui/ui_text.c ../../firmware/ui/ui_text.h \
               ../../firmware/ui/font_atlas.c ../../firmware/ui/font_atlas.h \
               ../../firmware/ui/launcher_assets.c \
               ../../firmware/ui/launcher_assets.h \
               ../../firmware/db/hdb.c ../../firmware/db/hdb.h \
               ../../firmware/diag/screen.c ../../firmware/lib/crc32.c \
               ../../firmware/lib/mem.c ../../firmware/lib/string.c \
               $(BLOB_OBJ)
	$(CC) $(CFLAGS) -o $@ test_music.c \
	      ../../firmware/ui/ui_text.c ../../firmware/ui/font_atlas.c \
	      ../../firmware/ui/launcher_assets.c \
	      ../../firmware/db/hdb.c ../../firmware/diag/screen.c \
	      ../../firmware/lib/crc32.c ../../firmware/lib/mem.c \
	      ../../firmware/lib/string.c $(BLOB_OBJ)

$(AUDIO_TEST): test_audio.c ../../firmware/ui/music.c \
               ../../firmware/ui/music.h ../../firmware/player/player.c \
               ../../firmware/player/player.h ../../firmware/player/wav.c \
               ../../firmware/player/read_ahead.c \
               ../../firmware/player/read_ahead.h \
               ../../firmware/player/wav.h ../../firmware/hal/6g/audio.h \
               ../../firmware/codec/codec.c \
               ../../firmware/codec/aiff/aiff_codec.c \
               ../../firmware/codec/flac/flac_codec.c \
               ../../firmware/codec/flac/flac_decoder.c \
               ../../firmware/codec/mp3/mp3_codec.c \
               ../../firmware/codec/mp3/mp3.h \
               ../../firmware/codec/tag/id3v2.c \
               ../../firmware/codec/tag/id3v2.h \
               ../../firmware/ui/launcher_assets.c \
               ../../firmware/ui/launcher_assets.h \
               ../../firmware/db/hdb.c ../../firmware/db/hdb.h \
               ../../firmware/diag/screen.c ../../firmware/lib/crc32.c \
               ../../firmware/lib/mem.c ../../firmware/lib/string.c \
               $(BLOB_OBJ) $(LIBMAD_OBJ)
	$(CC) $(CFLAGS) -o $@ test_audio.c \
	      ../../firmware/player/player.c ../../firmware/player/wav.c \
	      ../../firmware/player/read_ahead.c \
	      ../../firmware/ui/ui_text.c ../../firmware/ui/font_atlas.c \
	      ../../firmware/codec/codec.c \
	      ../../firmware/codec/aiff/aiff_codec.c \
	      ../../firmware/codec/flac/flac_codec.c \
	      ../../firmware/codec/flac/flac_decoder.c \
	      ../../firmware/codec/mp3/mp3_codec.c \
	      ../../firmware/codec/tag/id3v2.c \
	      ../../firmware/ui/launcher_assets.c \
	      ../../firmware/db/hdb.c ../../firmware/diag/screen.c \
	      ../../firmware/lib/crc32.c ../../firmware/lib/mem.c \
	      ../../firmware/lib/string.c $(BLOB_OBJ) $(LIBMAD_OBJ)

clean:
	rm -rf test_launcher build_mad
