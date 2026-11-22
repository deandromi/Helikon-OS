# SPDX-License-Identifier: GPL-2.0-or-later
# Host tests for the codec registry, WAV descriptor, native FLAC decoder and
# the MP3 (libmad) codec.

CC ?= cc
CFLAGS := -O2 -g -std=c99 -Wall -Wextra -Werror -Wconversion -Wshadow \
          -Wundef -Wpointer-arith -DHELIKON_HOST_TEST -I../../firmware/codec -I../../firmware/codec/tag \
          -I../../firmware/codec/flac -I../../firmware/codec/mp3 \
          -I../../firmware/codec/libmad -I../../firmware/lib \
          -I../../firmware/player

# Vendored libmad object rules are shared with every suite that links codec.c.
include ../libmad.mk

.PHONY: test sanitize clean

test: test_codec
	./test_codec

sanitize: test_codec_sanitize
	./test_codec_sanitize

SANFLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer

test_src = test_codec.c ../../firmware/codec/codec.c \
           ../../firmware/codec/flac/flac_codec.c \
           ../../firmware/codec/flac/flac_decoder.c \
           ../../firmware/codec/mp3/mp3_codec.c \
           ../../firmware/codec/tag/id3v2.c \
           ../../firmware/codec/tag/vorbis.c \
           ../../firmware/player/wav.c ../../firmware/lib/mem.c

test_deps = $(test_src) ../../firmware/codec/codec.h \
            ../../firmware/codec/flac/flac_decoder.h \
            ../../firmware/codec/mp3/mp3.h \
            ../../firmware/codec/tag/id3v2.h \
            ../../firmware/codec/tag/vorbis.h \
            ../../firmware/codec/libmad/libmad_port.h \
            ../../firmware/player/wav.h ../../firmware/lib/helikon_error.h

test_codec: $(test_deps) $(LIBMAD_OBJ)
	$(CC) $(CFLAGS) -o $@ $(test_src) $(LIBMAD_OBJ)

test_codec_sanitize: CFLAGS += $(SANFLAGS)
test_codec_sanitize: $(test_deps) $(SAN_LIBMAD_OBJ)
	$(CC) $(CFLAGS) -o $@ $(test_src) $(SAN_LIBMAD_OBJ)

clean:
	rm -rf test_codec test_codec_sanitize build_mad build_mad_sanitize
