# SPDX-License-Identifier: GPL-2.0-or-later
# Host tests for the codec registry, WAV descriptor, native FLAC decoder and
# the MP3 (libmad) codec.

CC ?= cc
CFLAGS := -O2 -g -std=c99 -Wall -Wextra -Werror -Wconversion -Wshadow \
          -Wundef -Wpointer-arith -DHELIKON_HOST_TEST -I../../firmware/codec \
          -I../../firmware/codec/flac -I../../firmware/codec/mp3 \
          -I../../firmware/codec/libmad -I../../firmware/lib \
          -I../../firmware/player

# Vendored libmad compiles with the 2004 warnings level of its upstream;
# suppress the modern narrowing diagnostics instead of editing the sources.
LIBMAD_CFLAGS := -O2 -g -std=c99 -DNDEBUG -Wno-sign-conversion -Wno-conversion \
                 -Wno-implicit-int-conversion -I../../firmware/codec/libmad \
                 -I../../firmware/lib
LIBMAD_SRC := bit.c frame.c huffman.c layer12.c layer3.c stream.c synth.c
LIBMAD_OBJ := build_mad/bit.o build_mad/frame.o build_mad/huffman.o \
              build_mad/layer12.o build_mad/layer3.o build_mad/stream.o \
              build_mad/synth.o
SAN_LIBMAD_OBJ := build_mad_sanitize/bit.o build_mad_sanitize/frame.o \
                  build_mad_sanitize/huffman.o build_mad_sanitize/layer12.o \
                  build_mad_sanitize/layer3.o build_mad_sanitize/stream.o \
                  build_mad_sanitize/synth.o

.PHONY: test sanitize clean

test: test_codec
	./test_codec

sanitize: test_codec_sanitize
	./test_codec_sanitize

build_mad build_mad_sanitize:
	@mkdir -p $@

build_mad/%.o: ../../firmware/codec/libmad/%.c | build_mad
	$(CC) $(LIBMAD_CFLAGS) -c $< -o $@

build_mad_sanitize/%.o: ../../firmware/codec/libmad/%.c | build_mad_sanitize
	$(CC) $(LIBMAD_CFLAGS) $(SANFLAGS) -c $< -o $@

SANFLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer

test_src = test_codec.c ../../firmware/codec/codec.c \
           ../../firmware/codec/flac/flac_codec.c \
           ../../firmware/codec/flac/flac_decoder.c \
           ../../firmware/codec/mp3/mp3_codec.c \
           ../../firmware/player/wav.c ../../firmware/lib/mem.c

test_deps = $(test_src) ../../firmware/codec/codec.h \
            ../../firmware/codec/flac/flac_decoder.h \
            ../../firmware/codec/mp3/mp3.h \
            ../../firmware/codec/libmad/libmad_port.h \
            ../../firmware/player/wav.h ../../firmware/lib/error.h

test_codec: $(test_deps) $(LIBMAD_OBJ)
	$(CC) $(CFLAGS) -o $@ $(test_src) $(LIBMAD_OBJ)

test_codec_sanitize: CFLAGS += $(SANFLAGS)
test_codec_sanitize: $(test_deps) $(SAN_LIBMAD_OBJ)
	$(CC) $(CFLAGS) -o $@ $(test_src) $(SAN_LIBMAD_OBJ)

clean:
	rm -rf test_codec test_codec_sanitize build_mad build_mad_sanitize
