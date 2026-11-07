# SPDX-License-Identifier: GPL-2.0-or-later
# Host tests for the player container parser.

CC ?= cc
CFLAGS := -O2 -g -std=c99 -Wall -Wextra -Werror -Wconversion -Wshadow \
          -Wundef -Wpointer-arith -I../../firmware/player -I../../firmware/lib

.PHONY: test clean

test: test_wav
	./test_wav

test_wav: test_wav.c ../../firmware/player/wav.c ../../firmware/player/wav.h
	$(CC) $(CFLAGS) -o $@ test_wav.c ../../firmware/player/wav.c

clean:
	rm -f test_wav
