# SPDX-License-Identifier: GPL-2.0-or-later
# Host tests for the codec registry, WAV descriptor and native FLAC decoder.

CC ?= cc
CFLAGS := -O2 -g -std=c99 -Wall -Wextra -Werror -Wconversion -Wshadow \
          -Wundef -Wpointer-arith -I../../firmware/codec \
          -I../../firmware/codec/flac -I../../firmware/lib \
          -I../../firmware/player

.PHONY: test clean

test: test_codec
	./test_codec

test_codec: test_codec.c ../../firmware/codec/codec.c \
            ../../firmware/codec/codec.h \
            ../../firmware/codec/flac/flac_codec.c \
            ../../firmware/codec/flac/flac_decoder.c \
            ../../firmware/codec/flac/flac_decoder.h \
            ../../firmware/player/wav.c ../../firmware/player/wav.h \
            ../../firmware/lib/mem.c ../../firmware/lib/error.h
	$(CC) $(CFLAGS) -o $@ test_codec.c ../../firmware/codec/codec.c \
	      ../../firmware/codec/flac/flac_codec.c \
	      ../../firmware/codec/flac/flac_decoder.c \
	      ../../firmware/player/wav.c ../../firmware/lib/mem.c

clean:
	rm -f test_codec
