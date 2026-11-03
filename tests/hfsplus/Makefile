# SPDX-License-Identifier: GPL-2.0-or-later
CC ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -std=c99 -Wall -Wextra -Werror -Wconversion -Wshadow
CPPFLAGS += -I../../firmware/hfs
HFS = ../../firmware/hfs
TESTS = test_volume test_btree

.PHONY: all test sanitize clean

all: $(TESTS)

test_volume: test_volume.c test_image.c test_image.h test_check.h \
             $(HFS)/hfs_volume.c $(HFS)/hfs_volume.h
	$(CC) $(CPPFLAGS) $(CFLAGS) test_volume.c test_image.c \
	  $(HFS)/hfs_volume.c $(LDFLAGS) -o $@

test_btree: test_btree.c test_image.c test_image.h test_check.h \
            $(HFS)/hfs_btree.c $(HFS)/hfs_btree.h \
            $(HFS)/hfs_volume.c $(HFS)/hfs_volume.h
	$(CC) $(CPPFLAGS) $(CFLAGS) test_btree.c test_image.c \
	  $(HFS)/hfs_btree.c $(HFS)/hfs_volume.c $(LDFLAGS) -o $@

test: $(TESTS)
	./test_volume
	./test_btree

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
SAN_CFLAGS = -O1 -g -std=c99 -Wall -Wextra -Werror -Wconversion -Wshadow \
             -fsanitize=address,undefined -fno-omit-frame-pointer
SAN_LDFLAGS = -fsanitize=address,undefined
else
SAN_CFLAGS = -O1 -g -std=c99 -Wall -Wextra -Werror -Wconversion -Wshadow \
             -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie
SAN_LDFLAGS = -fsanitize=address,undefined -no-pie
endif

sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS='$(SAN_CFLAGS)' LDFLAGS='$(SAN_LDFLAGS)' test

clean:
	rm -rf $(TESTS) *.dSYM
