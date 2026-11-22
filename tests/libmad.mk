# SPDX-License-Identifier: GPL-2.0-or-later
# Shared libmad object rules for the host suites.
#
# Vendored libmad compiles at its 2004 warning level; the modern narrowing
# diagnostics are suppressed rather than editing upstream sources. Included by
# every suite that links codec.c, because the registry references the MP3
# descriptor even when the test never decodes an MP3.
#
# The rules deliberately define no directory targets: an included file's first
# target would become the including Makefile's default goal.

LIBmaddir := ../../firmware/codec/libmad

LIBMAD_CFLAGS := -O2 -g -std=c99 -DNDEBUG -Wno-sign-conversion -Wno-conversion \
                 -Wno-implicit-int-conversion -I$(LIBmaddir) -I../../firmware/lib
LIBMAD_SRC := bit.c frame.c huffman.c layer12.c layer3.c stream.c synth.c
LIBMAD_OBJ := build_mad/bit.o build_mad/frame.o build_mad/huffman.o \
              build_mad/layer12.o build_mad/layer3.o build_mad/stream.o \
              build_mad/synth.o
SAN_LIBMAD_OBJ := build_mad_sanitize/bit.o build_mad_sanitize/frame.o \
                  build_mad_sanitize/huffman.o build_mad_sanitize/layer12.o \
                  build_mad_sanitize/layer3.o build_mad_sanitize/stream.o \
                  build_mad_sanitize/synth.o

build_mad/%.o: $(LIBmaddir)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(LIBMAD_CFLAGS) -c $< -o $@

# Vendored libmad is sanitized for memory safety only. Its fixed-point DSP
# shifts and multiplies mad_fixed_t values that a corrupt bitstream can drive
# out of range, so UBSan reports pages of signed-overflow notes on any mutated
# input - all upstream behaviour the project has decided not to carry patches
# for (see firmware/codec/libmad/README.md). AddressSanitizer stays on, because
# a read or write past a buffer inside the decoder is exactly what the mutation
# sweep is looking for.
LIBMAD_SANFLAGS := -fsanitize=address -fno-omit-frame-pointer

build_mad_sanitize/%.o: $(LIBmaddir)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(LIBMAD_CFLAGS) $(LIBMAD_SANFLAGS) -c $< -o $@
