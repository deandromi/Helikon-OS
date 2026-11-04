# SPDX-License-Identifier: GPL-2.0-or-later
#
# Helikon-OS F1 firmware build.
#
#   make -C firmware BOARD=6g            # ELF + raw image
#   make -C firmware BOARD=6g size       # map/size/headroom report
#   make -C firmware BOARD=6g test       # host tests for freestanding modules
#   make -C firmware BOARD=6g image      # signed development image
#   make -C firmware BOARD=6g clean
#
# The target toolchain is the pinned Rockbox arm-elf-eabi-gcc 9.5.0. The host
# test target uses the platform C compiler with the same freestanding sources.

BOARD ?= 6g
VERSION ?= 1
CROSS ?= arm-elf-eabi-
CC := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
NM := $(CROSS)nm
SIZE := $(CROSS)size
BUILD ?= ../build/firmware/$(BOARD)
OUT := $(BUILD)/out

GCC_INCLUDE := $(shell $(CC) -print-file-name=include)
LIBGCC := $(shell $(CC) -mcpu=arm926ej-s -marm -print-libgcc-file-name)

ARCH := -mcpu=arm926ej-s -marm -mno-thumb-interwork -mfloat-abi=soft
CFLAGS := -std=c99 -O2 -g -ffreestanding -fno-builtin -nostdinc \
          -isystem $(GCC_INCLUDE) \
          -Ilib -Iboot -Iboard/$(BOARD) -Ihal/$(BOARD) -Iblock -Idiag -Ihfs \
          -Wall -Wextra -Werror -Wconversion -Wshadow -Wundef \
          -Wpointer-arith -ffunction-sections -fdata-sections -fno-common \
          -fno-stack-protector -fno-unwind-tables \
          -fno-asynchronous-unwind-tables
LDFLAGS := $(ARCH) -nostdlib -T boot/firmware.ld -Wl,--gc-sections \
           -Wl,-Map=$(BUILD)/firmware.map -Wl,--build-id=none

BOOT_SRC := boot/start.S boot/vectors.S boot/boot_info.c boot/panic.c \
            boot/stack.c boot/firmware_main.c
LIB_SRC := lib/mem.c lib/string.c lib/crc32.c lib/ring.c lib/pool.c \
           lib/fixed.c lib/format.c
BOARD_SRC := board/$(BOARD)/board.c
HAL_SRC := hal/$(BOARD)/cpu.c hal/$(BOARD)/clock.c hal/$(BOARD)/pmu.c \
           hal/$(BOARD)/backlight.c hal/$(BOARD)/lcd.c hal/$(BOARD)/ata.c \
           hal/$(BOARD)/buttons.c
BLOCK_SRC := block/block.c block/bounce.c block/partition.c
DIAG_SRC := diag/stages.c diag/trace.c diag/crash.c diag/screen.c
HFS_ADAPTER_SRC := hfs/hfs_block_adapter.c hfs/hfs_file.c
HFS_SRC := hfs/hfs_volume.c hfs/hfs_btree.c hfs/hfs_catalog.c \
           hfs/hfs_extents.c hfs/hfs_unicode.c hfs/hfs_unicode_data.c

ALL_SRC := $(BOOT_SRC) $(LIB_SRC) $(BOARD_SRC) $(HAL_SRC) $(BLOCK_SRC) \
           $(DIAG_SRC) $(HFS_ADAPTER_SRC) $(HFS_SRC)
OBJS := $(patsubst %.c,$(BUILD)/%.o,$(patsubst %.S,$(BUILD)/%.o,$(ALL_SRC)))
ELF := $(BUILD)/firmware.elf
RAW := $(BUILD)/firmware.raw

.PHONY: all size test image clean

all: $(ELF) $(RAW)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(ARCH) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ARCH) $(CFLAGS) -c $< -o $@

$(ELF): $(OBJS) boot/firmware.ld
	@mkdir -p $(BUILD)
	$(CC) $(LDFLAGS) $(OBJS) $(LIBGCC) -o $@

$(RAW): $(ELF)
	$(OBJCOPY) -O binary --only-section=.text --only-section=.rodata \
	  --only-section=.data $< $@
	@python3 ../tools/firmware-size.py --nm $(NM) --elf $< --raw $@

size: $(ELF) $(RAW)
	@$(SIZE) $(ELF)
	@python3 ../tools/firmware-size.py --nm $(NM) --elf $(ELF) --raw $(RAW)

test:
	$(MAKE) -C ../tests/firmware test

image: all
	@mkdir -p $(OUT)
	python3 ../tools/make-firmware-image.py --image $(RAW) --out $(OUT) \
	  --board $(BOARD) --version $(VERSION) --load 0x08000000 --key-id 0x48454c01
	@python3 ../tools/firmware-size.py --nm $(NM) --elf $(ELF) --raw $(RAW)

clean:
	rm -rf $(BUILD)
