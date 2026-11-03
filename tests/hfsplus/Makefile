# SPDX-License-Identifier: GPL-2.0-or-later
CC ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -std=c99 -Wall -Wextra -Werror -Wconversion -Wshadow
CPPFLAGS += -I../../firmware/hfs
HFS = ../../firmware/hfs
TESTS = test_volume test_btree test_unicode test_catalog test_extents test_journal test_txn test_bitmap

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

test_unicode: test_unicode.c test_check.h \
              $(HFS)/hfs_unicode.c $(HFS)/hfs_unicode.h \
              $(HFS)/hfs_volume.h
	$(CC) $(CPPFLAGS) $(CFLAGS) test_unicode.c \
	  $(HFS)/hfs_unicode.c $(LDFLAGS) -o $@

test_catalog: test_catalog.c test_image.c test_fs_image.c \
              test_image.h test_fs_image.h test_check.h \
              $(HFS)/hfs_catalog.c $(HFS)/hfs_catalog.h \
              $(HFS)/hfs_unicode.c $(HFS)/hfs_unicode.h \
              $(HFS)/hfs_btree.c $(HFS)/hfs_btree.h \
              $(HFS)/hfs_volume.c $(HFS)/hfs_volume.h
	$(CC) $(CPPFLAGS) $(CFLAGS) test_catalog.c test_image.c test_fs_image.c \
	  $(HFS)/hfs_catalog.c $(HFS)/hfs_unicode.c $(HFS)/hfs_btree.c \
	  $(HFS)/hfs_volume.c $(LDFLAGS) -o $@

test_extents: test_extents.c test_image.c test_image.h test_check.h \
              $(HFS)/hfs_extents.c $(HFS)/hfs_extents.h \
              $(HFS)/hfs_btree.c $(HFS)/hfs_btree.h \
              $(HFS)/hfs_volume.c $(HFS)/hfs_volume.h
	$(CC) $(CPPFLAGS) $(CFLAGS) test_extents.c test_image.c \
	  $(HFS)/hfs_extents.c $(HFS)/hfs_btree.c $(HFS)/hfs_volume.c \
	  $(LDFLAGS) -o $@

test_journal: test_journal.c test_check.h \
              $(HFS)/hfs_journal.c $(HFS)/hfs_journal.h \
              $(HFS)/hfs_volume.c $(HFS)/hfs_volume.h
	$(CC) $(CPPFLAGS) $(CFLAGS) test_journal.c \
	  $(HFS)/hfs_journal.c $(HFS)/hfs_volume.c $(LDFLAGS) -o $@

test_txn: test_txn.c test_check.h \
          $(HFS)/hfs_txn.c $(HFS)/hfs_txn.h \
          $(HFS)/hfs_journal.c $(HFS)/hfs_journal.h \
          $(HFS)/hfs_volume.c $(HFS)/hfs_volume.h
	$(CC) $(CPPFLAGS) $(CFLAGS) test_txn.c \
	  $(HFS)/hfs_txn.c $(HFS)/hfs_journal.c $(HFS)/hfs_volume.c \
	  $(LDFLAGS) -o $@

test_bitmap: test_bitmap.c test_image.c test_image.h test_check.h \
             $(HFS)/hfs_bitmap.c $(HFS)/hfs_bitmap.h \
             $(HFS)/hfs_txn.c $(HFS)/hfs_txn.h \
             $(HFS)/hfs_journal.c $(HFS)/hfs_journal.h \
             $(HFS)/hfs_volume.c $(HFS)/hfs_volume.h
	$(CC) $(CPPFLAGS) $(CFLAGS) test_bitmap.c test_image.c \
	  $(HFS)/hfs_bitmap.c $(HFS)/hfs_txn.c $(HFS)/hfs_journal.c \
	  $(HFS)/hfs_volume.c $(LDFLAGS) -o $@

test: $(TESTS)
	./test_volume
	./test_btree
	./test_unicode
	./test_catalog
	./test_extents
	./test_journal
	./test_txn
	./test_bitmap

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
