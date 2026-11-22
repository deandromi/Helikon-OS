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

test: test_codec test_codec_mutation
	./test_codec
	./test_codec_mutation

# A finding is a failure, not a note. The vendored libmad objects are built
# with AddressSanitizer only (see ../libmad.mk), so every remaining undefined
# behaviour report is this project's code and should stop the suite.
SAN_ENV = UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
          ASAN_OPTIONS=fast_unwind_on_fatal=1

sanitize: test_codec_sanitize test_codec_mutation_sanitize
	$(SAN_ENV) ./test_codec_sanitize
	$(SAN_ENV) ./test_codec_mutation_sanitize

SANFLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer

codec_src = ../../firmware/codec/codec.c \
            ../../firmware/codec/flac/flac_codec.c \
            ../../firmware/codec/flac/flac_decoder.c \
            ../../firmware/codec/mp3/mp3_codec.c \
            ../../firmware/codec/tag/id3v2.c \
            ../../firmware/codec/tag/vorbis.c \
            ../../firmware/player/wav.c ../../firmware/lib/mem.c

test_src = test_codec.c $(codec_src)
mutation_src = test_codec_mutation.c $(codec_src)

test_deps = $(test_src) ../../firmware/codec/codec.h \
            ../../firmware/codec/flac/flac_decoder.h \
            ../../firmware/codec/mp3/mp3.h \
            ../../firmware/codec/tag/id3v2.h \
            ../../firmware/codec/tag/vorbis.h \
            ../../firmware/codec/libmad/libmad_port.h \
            ../../firmware/player/wav.h ../../firmware/lib/helikon_error.h

mutation_deps = $(mutation_src) $(test_deps)

test_codec: $(test_deps) $(LIBMAD_OBJ)
	$(CC) $(CFLAGS) -o $@ $(test_src) $(LIBMAD_OBJ)

test_codec_mutation: $(mutation_deps) $(LIBMAD_OBJ)
	$(CC) $(CFLAGS) -o $@ $(mutation_src) $(LIBMAD_OBJ)

test_codec_sanitize: CFLAGS += $(SANFLAGS)
test_codec_sanitize: $(test_deps) $(SAN_LIBMAD_OBJ)
	$(CC) $(CFLAGS) -o $@ $(test_src) $(SAN_LIBMAD_OBJ)

test_codec_mutation_sanitize: CFLAGS += $(SANFLAGS)
test_codec_mutation_sanitize: $(mutation_deps) $(SAN_LIBMAD_OBJ)
	$(CC) $(CFLAGS) -o $@ $(mutation_src) $(SAN_LIBMAD_OBJ)

clean:
	rm -rf test_codec test_codec_sanitize test_codec_mutation \
	       test_codec_mutation_sanitize build_mad build_mad_sanitize
