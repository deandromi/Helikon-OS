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
# The Disk Mode image gets its own build directory: the flag changes the code
# in `boot/firmware_main.c`, and a stale object from the other variant would
# produce an image that claims to be one thing and behaves as the other.
BUILD ?= ../build/firmware/$(BOARD)$(if $(filter 1,$(DISKMODE)),-diskmode,)
OUT := $(BUILD)/out

GCC_INCLUDE := $(shell $(CC) -print-file-name=include)
LIBGCC := $(shell $(CC) -mcpu=arm926ej-s -marm -print-libgcc-file-name)

ARCH := -mcpu=arm926ej-s -marm -mno-thumb-interwork -mfloat-abi=soft
CFLAGS := -std=c99 -O2 -g -ffreestanding -fno-builtin -nostdinc \
          -isystem $(GCC_INCLUDE) -DHELIKON_IMAGE_VERSION=$(VERSION) \
          -Ilib -Iboot -Iboard/$(BOARD) -Ihal/$(BOARD) -Iblock -Idiag -Ihfs \
          -Ivfs -Iui -Idb -Iplayer -Icodec -Iusb -Iupdate -Istorage -I../bootloader \
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
           hal/$(BOARD)/buttons.c hal/$(BOARD)/vic.c \
           hal/$(BOARD)/usb.c hal/$(BOARD)/usb_target.c
BLOCK_SRC := block/block.c block/bounce.c block/partition.c \
             block/ata_geometry.c
DIAG_SRC := diag/stages.c diag/trace.c diag/crash.c diag/screen.c
HFS_ADAPTER_SRC := hfs/hfs_block_adapter.c hfs/hfs_file.c
HFS_SRC := hfs/hfs_volume.c hfs/hfs_btree.c hfs/hfs_catalog.c \
           hfs/hfs_extents.c hfs/hfs_unicode.c hfs/hfs_unicode_data.c \
           hfs/hfs_catalog_compare.c \
           hfs/hfs_journal.c hfs/hfs_txn.c hfs/hfs_bitmap.c hfs/hfs_check.c \
           hfs/hfs_fork_io.c hfs/hfs_volume_write.c \
           hfs/hfs_btree_write.c hfs/hfs_btree_tree.c \
           hfs/hfs_extents_write.c hfs/hfs_catalog_write.c

UI_SRC := $(if $(filter 1,$(UI)),ui/launcher.c ui/launcher_run.c \
          ui/launcher_assets.c ui/ui_text.c ui/font_atlas.c ui/music.c,)
PLAYER_SRC :=
ifneq ($(AUDIO),)
PLAYER_SRC := player/player.c player/wav.c player/vfs_media.c \
              player/read_ahead.c player/queue.c player/playlist.c \
              player/cuesheet.c player/bookmark.c player/session.c \
              codec/codec.c codec/aiff/aiff_codec.c \
              codec/flac/flac_codec.c \
              codec/flac/flac_decoder.c \
              codec/mp3/mp3_codec.c codec/tag/id3v2.c \
              codec/tag/vorbis.c \
              codec/libmad/bit.c codec/libmad/frame.c \
              codec/libmad/huffman.c codec/libmad/layer12.c \
              codec/libmad/layer3.c codec/libmad/stream.c \
              codec/libmad/synth.c hal/$(BOARD)/audio.c
endif
# Compiled always so it cannot rot; without a caller the linker drops it.
LIBRARY_SRC := db/hdb.c db/hdb_vfs.c library/library.c \
               library/missing_tracks.c library/missing_tracks_io.c \
               library/music_source.c
# The write half, plus the two destinations that prove it has callers the player
# will use (T-093). All are garbage-collected until something calls them.
VFS_SRC := vfs/vfs.c vfs/vfs_rw.c player/vfs_playlist.c player/vfs_bookmark.c
# Mounting the volume at boot: block device -> partition -> HFS+ -> VFS ->
# catalog (T-126). Nothing calls it yet, so the linker drops it; the host test
# `tests/library` drives this exact path over a real image.
STORAGE_SRC := storage/mount.c storage/ata_block.c storage/music_boot.c \
               storage/write_test.c
# The signed staged-update client (T-064). Compiled always so it cannot rot;
# without a caller the linker drops it. The manifest and Ed25519 verifiers are
# the loader's own files, shared rather than copied: one implementation, one
# opinion about what a signed set is.
UPDATE_SRC := update/update.c ../bootloader/manifest.c ../bootloader/keys.c \
              ../bootloader/ed25519.c ../bootloader/loader_error.c
# Helikon Disk Mode image (D-54): the whole disk as a writable USB LUN, for a
# restore/format from a laptop. A separate image on purpose — the normal
# firmware never exposes a writable LUN. `make -C firmware BOARD=6g DISKMODE=1`.
ifeq ($(DISKMODE),1)
CFLAGS += -DHELIKON_DISK_MODE_IMAGE
endif

# The USB device stack. Compiled always so it cannot rot; without DISKMODE it
# is garbage-collected out of the image because nothing calls it (T-110).
USB_SRC := usb/usb_descriptors.c usb/usb_control.c usb/usb_msc.c \
           usb/disk_mode.c

ALL_SRC := $(BOOT_SRC) $(LIB_SRC) $(BOARD_SRC) $(HAL_SRC) $(BLOCK_SRC) \
           $(DIAG_SRC) $(HFS_ADAPTER_SRC) $(HFS_SRC) $(UI_SRC) \
           $(PLAYER_SRC) $(LIBRARY_SRC) $(VFS_SRC) $(STORAGE_SRC) $(USB_SRC) \
           $(UPDATE_SRC)
OBJS := $(patsubst %.c,$(BUILD)/%.o,$(patsubst %.S,$(BUILD)/%.o,$(ALL_SRC)))

ifneq ($(HDB),)
CFLAGS += -DHELIKON_HDB_EMBEDDED
HDB_BLOB_SRC := $(BUILD)/hdb_blob.S
HDB_BLOB_OBJ := $(BUILD)/hdb_blob.o
OBJS += $(HDB_BLOB_OBJ)
endif

ELF := $(BUILD)/firmware.elf
RAW := $(BUILD)/firmware.raw
CONFIG_STAMP := $(BUILD)/config.stamp
# The object set and the compile flags both depend on these variables, so the
# stamp records them. A plain "config-$(BOARD)-v$(VERSION)" name only tracked
# the version: a UI/AUDIO/HDB build left objects that the next plain build
# linked, which silently produced images for the wrong configuration.
CONFIG_TEXT := BOARD=$(BOARD) VERSION=$(VERSION) UI=$(UI) \
               AUDIO=$(AUDIO) AUDIO_TRACK=$(AUDIO_TRACK) HDB=$(HDB) \
               EXTRA_CFLAGS=$(EXTRA_CFLAGS)

.PHONY: all size test image clean FORCE

all: $(ELF) $(RAW)

# Rewrite the stamp only when the configuration actually changed, so an
# unchanged rebuild stays incremental.
$(CONFIG_STAMP): FORCE
	@mkdir -p $(BUILD)
	@printf '%s\n' '$(CONFIG_TEXT)' > $@.tmp
	@if cmp -s $@.tmp $@; then rm -f $@.tmp; else mv $@.tmp $@; fi

FORCE:

$(BUILD)/%.o: %.c $(CONFIG_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(ARCH) $(CFLAGS) -c $< -o $@

# Vendored libmad predates the modern narrowing diagnostics; silence the
# conversion warnings for its objects, per-compiler extras, and keep assert
# out (no libc on the target).
LIBMAD_NOWARN := -DNDEBUG -Wno-sign-conversion -Wno-conversion
ifeq ($(findstring clang,$(shell $(CC) --version)),)
LIBMAD_NOWARN += -Wno-implicit-fallthrough -Wno-shadow
else
LIBMAD_NOWARN += -Wno-implicit-int-conversion
endif

$(BUILD)/codec/libmad/%.o: CFLAGS += $(LIBMAD_NOWARN)

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
