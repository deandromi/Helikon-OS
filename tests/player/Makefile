# SPDX-License-Identifier: GPL-2.0-or-later
# Host tests for the player container parser and bounded player.

CC ?= cc
BASE_CFLAGS := -std=c99 -Wall -Wextra -Werror -Wconversion -Wshadow \
               -Wundef -Wpointer-arith -DHELIKON_HOST_TEST \
               -I../../firmware/player \
               -idirafter ../../firmware/lib -I../../firmware/hal/6g \
               -I../../firmware/codec -I../../firmware/codec/flac \
               -I../../firmware/codec/mp3 -I../../firmware/codec/libmad
CFLAGS ?= -O2 -g
CFLAGS += $(BASE_CFLAGS) $(EXTRA_CFLAGS)

# codec.c's registry references the MP3 descriptor, so every suite that links
# the codec layer needs the libmad objects even when it decodes no MP3.
include ../libmad.mk

.PHONY: test sanitize clean

test: test_wav test_player test_read_ahead test_queue test_playlist \
      test_cuesheet
	./test_wav
	./test_player
	./test_read_ahead
	./test_queue
	./test_playlist
	./test_cuesheet

# The media path links the codec registry (libmad included), so it runs under
# the same sanitizers as the codec suite.
sanitize: clean
	$(MAKE) EXTRA_CFLAGS="-O1 -fsanitize=address,undefined -fno-omit-frame-pointer" test

test_wav: test_wav.c ../../firmware/player/wav.c ../../firmware/player/wav.h \
          ../../firmware/lib/mem.c
	$(CC) $(CFLAGS) -o $@ test_wav.c ../../firmware/player/wav.c \
	      ../../firmware/lib/mem.c

test_playlist: test_playlist.c ../../firmware/player/playlist.c \
               ../../firmware/player/playlist.h ../../firmware/lib/mem.c
	$(CC) $(CFLAGS) -o $@ test_playlist.c ../../firmware/player/playlist.c \
	      ../../firmware/lib/mem.c

test_cuesheet: test_cuesheet.c ../../firmware/player/cuesheet.c \
               ../../firmware/player/cuesheet.h ../../firmware/lib/mem.c
	$(CC) $(CFLAGS) -o $@ test_cuesheet.c ../../firmware/player/cuesheet.c \
	      ../../firmware/lib/mem.c

test_read_ahead: test_read_ahead.c ../../firmware/player/read_ahead.c \
                 ../../firmware/player/read_ahead.h
	$(CC) $(CFLAGS) -o $@ test_read_ahead.c \
	      ../../firmware/player/read_ahead.c

test_queue: test_queue.c ../../firmware/player/queue.c \
            ../../firmware/player/queue.h
	$(CC) $(CFLAGS) -o $@ test_queue.c ../../firmware/player/queue.c

test_player: test_player.c ../../firmware/player/player.c \
             ../../firmware/player/player.h ../../firmware/player/wav.c \
             ../../firmware/player/read_ahead.c \
             ../../firmware/player/read_ahead.h \
             ../../firmware/player/wav.h ../../firmware/hal/6g/audio.h \
             ../../firmware/hal/6g/audio_dma.h \
             ../../firmware/hal/6g/audio_stream.h \
             ../../firmware/codec/codec.c ../../firmware/codec/codec.h \
             ../../firmware/codec/flac/flac_codec.c \
             ../../firmware/codec/flac/flac_decoder.c \
             ../../firmware/codec/flac/flac_decoder.h \
             ../../firmware/codec/mp3/mp3_codec.c ../../firmware/codec/mp3/mp3.h \
             ../../firmware/lib/mem.c $(LIBMAD_OBJ)
	$(CC) $(CFLAGS) -o $@ test_player.c ../../firmware/player/player.c \
	      ../../firmware/player/wav.c ../../firmware/player/read_ahead.c \
	      ../../firmware/codec/codec.c \
	      ../../firmware/codec/flac/flac_codec.c \
	      ../../firmware/codec/flac/flac_decoder.c \
	      ../../firmware/codec/mp3/mp3_codec.c \
	      ../../firmware/codec/tag/id3v2.c \
	      ../../firmware/lib/mem.c $(LIBMAD_OBJ)

clean:
	rm -rf test_wav test_player test_read_ahead test_queue test_playlist \
	  build_mad *.dSYM
