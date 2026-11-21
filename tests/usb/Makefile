# SPDX-License-Identifier: GPL-2.0-or-later
# Host tests for the USB device core: descriptors, chapter 9 control requests and
# the bulk-only mass-storage path. No controller and no device are involved.

CC ?= cc
CFLAGS := -O2 -g -std=c99 -Wall -Wextra -Werror -Wconversion -Wshadow \
          -Wundef -Wpointer-arith -DHELIKON_HOST_TEST \
          -I../../firmware/usb -I../../firmware/block -I../../firmware/lib \
          -idirafter ../../firmware/lib

BIN := test_usb
SAN_BIN := test_usb_sanitize
USB := ../../firmware/usb
BLOCK := ../../firmware/block
LIB := ../../firmware/lib

SOURCES := test_usb.c \
           $(USB)/usb_descriptors.c $(USB)/usb_control.c $(USB)/usb_msc.c \
           $(BLOCK)/block.c $(BLOCK)/bounce.c \
           $(LIB)/mem.c $(LIB)/string.c

.PHONY: all test sanitize clean

all: $(BIN)

$(BIN): $(SOURCES) $(USB)/usb_descriptors.h $(USB)/usb_control.h \
        $(USB)/usb_msc.h $(BLOCK)/block.h
	$(CC) $(CFLAGS) -o $@ $(SOURCES)

test: $(BIN)
	./$(BIN)

# A separate binary, so a sanitizer build never leaves a poisoned `test_usb`
# behind for the next `make test`.
$(SAN_BIN): $(SOURCES) $(USB)/usb_descriptors.h $(USB)/usb_control.h \
        $(USB)/usb_msc.h $(BLOCK)/block.h
	$(CC) $(CFLAGS) -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
	    -o $@ $(SOURCES)

sanitize:
	$(MAKE) clean
	$(MAKE) $(SAN_BIN)
	./$(SAN_BIN)

clean:
	rm -f $(BIN) $(SAN_BIN)
	rm -rf $(BIN).dSYM $(SAN_BIN).dSYM
