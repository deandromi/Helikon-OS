# SPDX-License-Identifier: GPL-2.0-or-later
# Host tests for the player container parser and bounded player.

CC ?= cc
CFLAGS := -O2 -g -std=c99 -Wall -Wextra -Werror -Wconversion -Wshadow \
          -Wundef -Wpointer-arith -I../../firmware/player -I../../firmware/lib \
          -I../../firmware/hal/6g -I../../firmware/codec -I../../firmware/codec/flac \
          -I../../firmware/codec/mp3 -I../../firmware/codec/libmad

# codec.c's registry references the MP3 descriptor, so every suite that links
# the codec layer needs the libmad objects even when it decodes no MP3.
include ../libmad.mk

.PHONY: test clean

test: test_wav test_player
	./test_wav
	./test_player

test_wav: test_wav.c ../../firmware/player/wav.c ../../firmware/player/wav.h \
          ../../firmware/lib/mem.c
	$(CC) $(CFLAGS) -o $@ test_wav.c ../../firmware/player/wav.c \
	      ../../firmware/lib/mem.c

test_player: test_player.c ../../firmware/player/player.c \
             ../../firmware/player/player.h ../../firmware/player/wav.c \
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
	      ../../firmware/player/wav.c ../../firmware/codec/codec.c \
	      ../../firmware/codec/flac/flac_codec.c \
	      ../../firmware/codec/flac/flac_decoder.c \
	      ../../firmware/codec/mp3/mp3_codec.c \
	      ../../firmware/codec/tag/id3v2.c \
	      ../../firmware/lib/mem.c $(LIBMAD_OBJ)

clean:
	rm -rf test_wav test_player build_mad
