# SPDX-License-Identifier: GPL-2.0-or-later
# Host tests for the player container parser and bounded player.

CC ?= cc
CFLAGS := -O2 -g -std=c99 -Wall -Wextra -Werror -Wconversion -Wshadow \
          -Wundef -Wpointer-arith -I../../firmware/player -I../../firmware/lib \
          -I../../firmware/hal/6g -I../../firmware/codec -I../../firmware/codec/flac

.PHONY: test clean

test: test_wav test_player
	./test_wav
	./test_player

test_wav: test_wav.c ../../firmware/player/wav.c ../../firmware/player/wav.h
	$(CC) $(CFLAGS) -o $@ test_wav.c ../../firmware/player/wav.c

test_player: test_player.c ../../firmware/player/player.c \
             ../../firmware/player/player.h ../../firmware/player/wav.c \
             ../../firmware/player/wav.h ../../firmware/hal/6g/audio.h \
             ../../firmware/hal/6g/audio_dma.h \
             ../../firmware/hal/6g/audio_stream.h \
             ../../firmware/codec/codec.c ../../firmware/codec/codec.h \
             ../../firmware/codec/flac/flac_codec.c \
             ../../firmware/codec/flac/flac_decoder.c \
             ../../firmware/codec/flac/flac_decoder.h \
             ../../firmware/lib/mem.c
	$(CC) $(CFLAGS) -o $@ test_player.c ../../firmware/player/player.c \
	      ../../firmware/player/wav.c ../../firmware/codec/codec.c \
	      ../../firmware/codec/flac/flac_codec.c \
	      ../../firmware/codec/flac/flac_decoder.c \
	      ../../firmware/lib/mem.c

clean:
	rm -f test_wav test_player
