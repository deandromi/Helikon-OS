# SPDX-License-Identifier: GPL-2.0-or-later
# Host tests for the USB device core: descriptors, chapter 9 control requests and
# the bulk-only mass-storage path. No controller and no device are involved.

CC ?= cc
CFLAGS := -O2 -g -std=c99 -Wall -Wextra -Werror -Wconversion -Wshadow \
          -Wundef -Wpointer-arith -DHELIKON_HOST_TEST \
          -I../../firmware/usb -I../../firmware/block -I../../firmware/lib \
          -I../../firmware/hal/6g -idirafter ../../firmware/lib

BIN := test_usb
SAN_BIN := test_usb_sanitize
HAL_BIN := test_usb_hal
HAL_SAN_BIN := test_usb_hal_sanitize
USB := ../../firmware/usb
BLOCK := ../../firmware/block
LIB := ../../firmware/lib

SOURCES := test_usb.c \
           $(USB)/usb_descriptors.c $(USB)/usb_control.c $(USB)/usb_msc.c \
           $(BLOCK)/block.c $(BLOCK)/bounce.c \
           $(LIB)/mem.c $(LIB)/string.c

HAL := ../../firmware/hal/6g
HAL_SOURCES := test_usb_hal.c $(HAL)/usb.c
MODE_BIN := test_disk_mode
MODE_SAN_BIN := test_disk_mode_sanitize
FUZZ_BIN := fuzz_msc
FUZZ_SAN_BIN := fuzz_msc_sanitize
FUZZ_SOURCES := fuzz_msc.c $(USB)/usb_msc.c $(BLOCK)/block.c $(BLOCK)/bounce.c \
                $(LIB)/mem.c $(LIB)/string.c

MODE_FUZZ_BIN := fuzz_mode
MODE_FUZZ_SAN_BIN := fuzz_mode_sanitize
MODE_FUZZ_SOURCES := fuzz_mode.c $(USB)/disk_mode.c $(USB)/usb_control.c \
                    $(USB)/usb_descriptors.c $(USB)/usb_msc.c \
                    $(BLOCK)/block.c $(BLOCK)/bounce.c $(LIB)/mem.c \
                    $(LIB)/string.c

MODE_SOURCES := test_disk_mode.c $(USB)/disk_mode.c $(USB)/usb_control.c \
               $(USB)/usb_descriptors.c $(USB)/usb_msc.c \
               $(BLOCK)/block.c $(BLOCK)/bounce.c $(LIB)/mem.c $(LIB)/string.c

.PHONY: all test sanitize fuzz fuzz-sanitize clean

all: $(BIN)

$(BIN): $(SOURCES) $(USB)/usb_descriptors.h $(USB)/usb_control.h \
        $(USB)/usb_msc.h $(BLOCK)/block.h
	$(CC) $(CFLAGS) -o $@ $(SOURCES)

test: $(BIN) $(HAL_BIN) $(MODE_BIN)
	./$(BIN)
	./$(HAL_BIN)
	./$(MODE_BIN)

# A separate binary, so a sanitizer build never leaves a poisoned `test_usb`
# behind for the next `make test`.
$(SAN_BIN): $(SOURCES) $(USB)/usb_descriptors.h $(USB)/usb_control.h \
        $(USB)/usb_msc.h $(BLOCK)/block.h
	$(CC) $(CFLAGS) -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
	    -o $@ $(SOURCES)

$(HAL_BIN): $(HAL_SOURCES) $(HAL)/usb.h $(HAL)/usb_hw.h
	$(CC) $(CFLAGS) -o $@ $(HAL_SOURCES)

$(HAL_SAN_BIN): $(HAL_SOURCES) $(HAL)/usb.h $(HAL)/usb_hw.h
	$(CC) $(CFLAGS) -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
	    -o $@ $(HAL_SOURCES)

$(MODE_BIN): $(MODE_SOURCES) $(USB)/disk_mode.h $(USB)/usb_msc.h \
        $(USB)/usb_control.h $(BLOCK)/block.h
	$(CC) $(CFLAGS) -o $@ $(MODE_SOURCES)

$(MODE_SAN_BIN): $(MODE_SOURCES) $(USB)/disk_mode.h $(USB)/usb_msc.h \
        $(USB)/usb_control.h $(BLOCK)/block.h
	$(CC) $(CFLAGS) -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
	    -o $@ $(MODE_SOURCES)

# Malformed CBWs, hostile CDB lengths and a data stage that stops halfway: the
# transport must refuse all of it, keep its window and never report a success it
# did not achieve. Deterministic, so a failure reproduces from the seed.
$(FUZZ_BIN): $(FUZZ_SOURCES) $(USB)/usb_msc.h $(BLOCK)/block.h
	$(CC) $(CFLAGS) -o $@ $(FUZZ_SOURCES)

$(MODE_FUZZ_BIN): $(MODE_FUZZ_SOURCES) $(USB)/disk_mode.h $(USB)/usb_msc.h \
        $(USB)/usb_control.h $(BLOCK)/block.h
	$(CC) $(CFLAGS) -o $@ $(MODE_FUZZ_SOURCES)

$(MODE_FUZZ_SAN_BIN): $(MODE_FUZZ_SOURCES) $(USB)/disk_mode.h \
        $(USB)/usb_msc.h $(USB)/usb_control.h $(BLOCK)/block.h
	$(CC) $(CFLAGS) -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
	    -o $@ $(MODE_FUZZ_SOURCES)

fuzz: $(FUZZ_BIN) $(MODE_FUZZ_BIN)
	./$(FUZZ_BIN)
	./$(MODE_FUZZ_BIN)

$(FUZZ_SAN_BIN): $(FUZZ_SOURCES) $(USB)/usb_msc.h $(BLOCK)/block.h
	$(CC) $(CFLAGS) -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
	    -o $@ $(FUZZ_SOURCES)

fuzz-sanitize:
	$(MAKE) $(FUZZ_SAN_BIN)
	./$(FUZZ_SAN_BIN) -n 25000
	$(MAKE) $(MODE_FUZZ_SAN_BIN)
	./$(MODE_FUZZ_SAN_BIN) -n 10000

sanitize:
	$(MAKE) clean
	$(MAKE) $(SAN_BIN)
	./$(SAN_BIN)
	$(MAKE) $(HAL_SAN_BIN)
	./$(HAL_SAN_BIN)
	$(MAKE) $(MODE_SAN_BIN)
	./$(MODE_SAN_BIN)
	$(MAKE) $(FUZZ_SAN_BIN)
	./$(FUZZ_SAN_BIN) -n 25000
	$(MAKE) $(MODE_FUZZ_SAN_BIN)
	./$(MODE_FUZZ_SAN_BIN) -n 10000

clean:
	rm -f $(BIN) $(SAN_BIN) $(HAL_BIN) $(HAL_SAN_BIN) $(MODE_BIN) $(MODE_SAN_BIN) \
	    $(FUZZ_BIN) $(FUZZ_SAN_BIN) $(MODE_FUZZ_BIN) $(MODE_FUZZ_SAN_BIN)
	rm -rf $(BIN).dSYM $(SAN_BIN).dSYM $(HAL_BIN).dSYM $(HAL_SAN_BIN).dSYM \
	    $(MODE_BIN).dSYM $(MODE_SAN_BIN).dSYM $(FUZZ_BIN).dSYM \
	    $(FUZZ_SAN_BIN).dSYM
