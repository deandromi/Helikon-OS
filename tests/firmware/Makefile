# SPDX-License-Identifier: GPL-2.0-or-later
#
# Host tests for the freestanding F1 firmware modules. The same sources are
# compiled with -DHELIKON_HOST_TEST so they use the helikon_* memory names and
# the host test driver can keep using stdio.

HOSTCC ?= cc
HOST_INCLUDE := $(shell $(HOSTCC) -print-file-name=include)
BUILD := build

CFLAGS := -O2 -g -std=c99 -Wall -Wextra -Werror -Wconversion -Wshadow \
          -Wundef -Wpointer-arith
FW_CFLAGS := $(CFLAGS) -ffreestanding -fno-builtin -nostdinc \
             -isystem $(HOST_INCLUDE) -DHELIKON_HOST_TEST \
             -I../../firmware/lib -I../../firmware/boot \
             -I../../firmware/block -I../../firmware/diag \
             -I../../firmware/hfs
TEST_CFLAGS := $(CFLAGS) -I../../firmware/lib -I../../firmware/boot \
               -I../../firmware/block -I../../firmware/diag \
               -I../../firmware/hfs

FW_OBJS := $(BUILD)/lib/mem.o $(BUILD)/lib/string.o $(BUILD)/lib/crc32.o \
           $(BUILD)/lib/sha256.o $(BUILD)/lib/ring.o $(BUILD)/lib/pool.o \
           $(BUILD)/lib/fixed.o $(BUILD)/lib/format.o \
           $(BUILD)/boot/boot_info.o $(BUILD)/boot/bootstate.o \
           $(BUILD)/block/block.o $(BUILD)/block/bounce.o \
           $(BUILD)/block/partition.o $(BUILD)/block/ata_geometry.o \
           $(BUILD)/diag/stages.o \
           $(BUILD)/diag/trace.o $(BUILD)/diag/crash.o \
           $(BUILD)/diag/screen.o $(BUILD)/hfs/hfs_block_adapter.o

TESTS := test_lib test_boot_info test_bootstate test_block test_ata_geometry \
         test_stages test_ring test_pool test_screen test_trace test_hfs_file

.PHONY: all test sanitize clean

all: $(TESTS)

$(BUILD)/%.o: ../../firmware/%.c
	@mkdir -p $(dir $@)
	$(HOSTCC) $(FW_CFLAGS) -c $< -o $@

test_lib: test_lib.c test_check.h $(BUILD)/lib/mem.o $(BUILD)/lib/string.o \
          $(BUILD)/lib/crc32.o $(BUILD)/lib/fixed.o $(BUILD)/lib/format.o
	$(HOSTCC) $(TEST_CFLAGS) test_lib.c $(BUILD)/lib/mem.o \
	  $(BUILD)/lib/string.o $(BUILD)/lib/crc32.o $(BUILD)/lib/fixed.o \
	  $(BUILD)/lib/format.o $(LDFLAGS) -o $@

test_boot_info: test_boot_info.c test_check.h $(BUILD)/boot/boot_info.o \
                $(BUILD)/lib/mem.o $(BUILD)/lib/crc32.o
	$(HOSTCC) $(TEST_CFLAGS) test_boot_info.c $(BUILD)/boot/boot_info.o \
	  $(BUILD)/lib/mem.o $(BUILD)/lib/crc32.o $(LDFLAGS) -o $@

test_bootstate: test_bootstate.c test_check.h $(BUILD)/boot/bootstate.o \
                $(BUILD)/lib/sha256.o $(BUILD)/lib/mem.o $(BUILD)/lib/string.o
	$(HOSTCC) $(TEST_CFLAGS) test_bootstate.c $(BUILD)/boot/bootstate.o \
	  $(BUILD)/lib/sha256.o $(BUILD)/lib/mem.o $(BUILD)/lib/string.o \
	  $(LDFLAGS) -o $@

test_block: test_block.c test_check.h $(BUILD)/block/block.o \
            $(BUILD)/block/bounce.o $(BUILD)/block/partition.o \
            $(BUILD)/lib/mem.o
	$(HOSTCC) $(TEST_CFLAGS) test_block.c $(BUILD)/block/block.o \
	  $(BUILD)/block/bounce.o $(BUILD)/block/partition.o \
	  $(BUILD)/lib/mem.o $(LDFLAGS) -o $@

test_ata_geometry: test_ata_geometry.c test_check.h \
                   $(BUILD)/block/ata_geometry.o
	$(HOSTCC) $(TEST_CFLAGS) test_ata_geometry.c \
	  $(BUILD)/block/ata_geometry.o $(LDFLAGS) -o $@

test_stages: test_stages.c test_check.h $(BUILD)/diag/stages.o
	$(HOSTCC) $(TEST_CFLAGS) test_stages.c $(BUILD)/diag/stages.o \
	  $(LDFLAGS) -o $@

test_ring: test_ring.c test_check.h $(BUILD)/lib/ring.o
	$(HOSTCC) $(TEST_CFLAGS) test_ring.c $(BUILD)/lib/ring.o $(LDFLAGS) -o $@

test_pool: test_pool.c test_check.h $(BUILD)/lib/pool.o
	$(HOSTCC) $(TEST_CFLAGS) test_pool.c $(BUILD)/lib/pool.o $(LDFLAGS) -o $@

test_screen: test_screen.c test_check.h $(BUILD)/diag/screen.o
	$(HOSTCC) $(TEST_CFLAGS) test_screen.c $(BUILD)/diag/screen.o \
	  $(LDFLAGS) -o $@

test_trace: test_trace.c test_check.h $(BUILD)/diag/trace.o \
            $(BUILD)/lib/ring.o $(BUILD)/lib/mem.o
	$(HOSTCC) $(TEST_CFLAGS) test_trace.c $(BUILD)/diag/trace.o \
	  $(BUILD)/lib/ring.o $(BUILD)/lib/mem.o $(LDFLAGS) -o $@

HFS_TEST_CFLAGS := $(CFLAGS) -I../../firmware/hfs \
                   -idirafter ../../firmware/lib -I../hfsplus
HFS_TEST_SRC := ../../firmware/hfs/hfs_file.c \
                ../../firmware/hfs/hfs_catalog.c \
                ../../firmware/hfs/hfs_unicode.c \
                ../../firmware/hfs/hfs_unicode_data.c \
                ../../firmware/hfs/hfs_extents.c \
                ../../firmware/hfs/hfs_btree.c \
                ../../firmware/hfs/hfs_volume.c \
                ../hfsplus/test_image.c ../hfsplus/test_fs_image.c

test_hfs_file: test_hfs_file.c test_check.h $(HFS_TEST_SRC)
	$(HOSTCC) $(HFS_TEST_CFLAGS) test_hfs_file.c $(HFS_TEST_SRC) \
	  $(LDFLAGS) -o $@

test: $(TESTS)
	./test_lib
	./test_boot_info
	./test_bootstate
	./test_block
	./test_ata_geometry
	./test_stages
	./test_ring
	./test_pool
	./test_screen
	./test_trace
	./test_hfs_file

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
SAN_CFLAGS = -O1 -g -std=c99 -Wall -Wextra -Werror -Wconversion -Wshadow \
             -Wundef -Wpointer-arith -fsanitize=address,undefined \
             -fno-omit-frame-pointer
SAN_LDFLAGS = -fsanitize=address,undefined
else
SAN_CFLAGS = -O1 -g -std=c99 -Wall -Wextra -Werror -Wconversion -Wshadow \
             -Wundef -Wpointer-arith -fsanitize=address,undefined \
             -fno-omit-frame-pointer -fno-pie
SAN_LDFLAGS = -fsanitize=address,undefined -no-pie
endif

sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS='$(SAN_CFLAGS)' FW_CFLAGS='$(SAN_CFLAGS) -ffreestanding -fno-builtin -nostdinc -isystem $(HOST_INCLUDE) -DHELIKON_HOST_TEST -I../../firmware/lib -I../../firmware/boot -I../../firmware/block -I../../firmware/diag -I../../firmware/hfs' LDFLAGS='$(SAN_LDFLAGS)' test

clean:
	rm -rf $(BUILD) $(TESTS) *.dSYM
