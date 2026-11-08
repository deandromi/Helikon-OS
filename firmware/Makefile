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
BOOTSTATE_STATE ?= good
EXTRA_CFLAGS ?=
UI ?= 0
HDB ?=
AUDIO ?=
AUDIO_TRACK ?= 0
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
          -isystem $(GCC_INCLUDE) -DHELIKON_IMAGE_VERSION=$(VERSION) \
          -Ilib -Iboot -Iboard/$(BOARD) -Ihal/$(BOARD) -Iblock -Idiag -Ihfs \
          -Iui -Idb -Iplayer -Icodec \
          -Wall -Wextra -Werror -Wconversion -Wshadow -Wundef \
          -Wpointer-arith -ffunction-sections -fdata-sections -fno-common \
          -fno-stack-protector -fno-unwind-tables \
          -fno-asynchronous-unwind-tables
CFLAGS += $(EXTRA_CFLAGS)
LDFLAGS := $(ARCH) -nostdlib -T boot/firmware.ld -Wl,--gc-sections \
           -Wl,-Map=$(BUILD)/firmware.map -Wl,--build-id=none

BOOT_SRC := boot/start.S boot/vectors.S boot/boot_info.c boot/bootstate.c \
            boot/panic.c boot/stack.c boot/firmware_main.c
LIB_SRC := lib/mem.c lib/string.c lib/crc32.c lib/sha256.c lib/ring.c \
           lib/pool.c lib/fixed.c lib/format.c
BOARD_SRC := board/$(BOARD)/board.c
HAL_SRC := hal/$(BOARD)/cpu.c hal/$(BOARD)/clock.c hal/$(BOARD)/pmu.c \
           hal/$(BOARD)/backlight.c hal/$(BOARD)/lcd.c hal/$(BOARD)/ata.c \
           hal/$(BOARD)/buttons.c
BLOCK_SRC := block/block.c block/bounce.c block/partition.c
DIAG_SRC := diag/stages.c diag/trace.c diag/crash.c diag/screen.c
HFS_ADAPTER_SRC := hfs/hfs_block_adapter.c hfs/hfs_file.c
HFS_SRC := hfs/hfs_volume.c hfs/hfs_btree.c hfs/hfs_catalog.c \
           hfs/hfs_extents.c hfs/hfs_unicode.c hfs/hfs_unicode_data.c \
           hfs/hfs_catalog_compare.c \
           hfs/hfs_journal.c hfs/hfs_txn.c hfs/hfs_bitmap.c \
           hfs/hfs_fork_io.c hfs/hfs_volume_write.c \
           hfs/hfs_btree_write.c hfs/hfs_btree_tree.c \
           hfs/hfs_extents_write.c hfs/hfs_catalog_write.c

UI_SRC := $(if $(filter 1,$(UI)),ui/launcher.c ui/launcher_run.c \
          ui/launcher_assets.c ui/music.c,)
PLAYER_SRC :=
ifneq ($(AUDIO),)
PLAYER_SRC := player/player.c player/wav.c \
              codec/codec.c codec/flac/flac_codec.c \
              codec/flac/flac_decoder.c hal/$(BOARD)/audio.c
endif
LIBRARY_SRC := db/hdb.c

ALL_SRC := $(BOOT_SRC) $(LIB_SRC) $(BOARD_SRC) $(HAL_SRC) $(BLOCK_SRC) \
           $(DIAG_SRC) $(HFS_ADAPTER_SRC) $(HFS_SRC) $(UI_SRC) \
           $(PLAYER_SRC) $(LIBRARY_SRC)
OBJS := $(patsubst %.c,$(BUILD)/%.o,$(patsubst %.S,$(BUILD)/%.o,$(ALL_SRC)))

ifneq ($(HDB),)
CFLAGS += -DHELIKON_HDB_EMBEDDED
HDB_BLOB_SRC := $(BUILD)/hdb_blob.S
HDB_BLOB_OBJ := $(BUILD)/hdb_blob.o
OBJS += $(HDB_BLOB_OBJ)
endif

ELF := $(BUILD)/firmware.elf
RAW := $(BUILD)/firmware.raw
CONFIG_STAMP := $(BUILD)/config-$(BOARD)-v$(VERSION)

.PHONY: all size test image clean

all: $(ELF) $(RAW)

$(CONFIG_STAMP):
	@mkdir -p $(BUILD)
	@touch $@

$(BUILD)/%.o: %.c $(CONFIG_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(ARCH) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S $(CONFIG_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(ARCH) $(CFLAGS) -c $< -o $@

ifneq ($(HDB),)
$(HDB_BLOB_SRC): $(HDB)
	@mkdir -p $(BUILD)
	@printf '.section .rodata, "a", %%progbits\n.balign 4\n.global helikon_hdb_start\nhelikon_hdb_start:\n.incbin "%s"\n.global helikon_hdb_end\nhelikon_hdb_end:\n.balign 4\n' "$(abspath $(HDB))" > $@

$(HDB_BLOB_OBJ): $(HDB_BLOB_SRC)
	$(CC) $(ARCH) -c $< -o $@
endif

ifneq ($(AUDIO),)
CFLAGS += -DHELIKON_AUDIO_EMBEDDED -DHELIKON_AUDIO_TRACK=$(AUDIO_TRACK)
AUDIO_BLOB_SRC := $(BUILD)/audio_blob.S
AUDIO_BLOB_OBJ := $(BUILD)/audio_blob.o
OBJS += $(AUDIO_BLOB_OBJ)

$(AUDIO_BLOB_SRC): $(AUDIO)
	@mkdir -p $(BUILD)
	@printf '.section .rodata, "a", %%progbits\n.balign 4\n.global helikon_audio_start\nhelikon_audio_start:\n.incbin "%s"\n.global helikon_audio_end\nhelikon_audio_end:\n.balign 4\n' "$(abspath $(AUDIO))" > $@

$(AUDIO_BLOB_OBJ): $(AUDIO_BLOB_SRC)
	$(CC) $(ARCH) -c $< -o $@
endif

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
	  --board $(BOARD) --version $(VERSION) --load 0x08000000 \
	  --bootstate-state $(BOOTSTATE_STATE) --key-id 0x48454c01
	@python3 ../tools/firmware-size.py --nm $(NM) --elf $(ELF) --raw $(RAW)

clean:
	rm -rf $(BUILD)
