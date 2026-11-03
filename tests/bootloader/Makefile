# SPDX-License-Identifier: GPL-2.0-or-later
CC ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -std=c99 -Wall -Wextra -Werror -Wconversion -Wshadow
CPPFLAGS += -I../../firmware/hfs -I../../bootloader -I../hfsplus
HFS = ../../firmware/hfs
SOURCES = test_diag.c ../hfsplus/test_image.c ../hfsplus/test_fs_image.c \
          ../../bootloader/diagnostics.c ../../bootloader/report.c ../../bootloader/splash.c \
          $(HFS)/hfs_catalog.c $(HFS)/hfs_unicode.c $(HFS)/hfs_extents.c \
          $(HFS)/hfs_btree.c $(HFS)/hfs_volume.c
HEADERS = ../../bootloader/diagnostics.h ../../bootloader/report.h ../../bootloader/splash.h \
          ../hfsplus/test_image.h ../hfsplus/test_fs_image.h \
          ../hfsplus/test_check.h $(HFS)/hfs_catalog.h $(HFS)/hfs_unicode.h \
          $(HFS)/hfs_extents.h $(HFS)/hfs_btree.h $(HFS)/hfs_volume.h

.PHONY: all test sanitize clean

all: test_diag

test_diag: $(SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) $(LDFLAGS) -o $@

test: test_diag
	./test_diag

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
	rm -rf test_diag *.dSYM
