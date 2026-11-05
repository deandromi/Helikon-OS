# SPDX-License-Identifier: GPL-2.0-or-later
# Host render of the firmware launcher. Produces build/ui/launcher.png.

CC ?= cc
CFLAGS := -O2 -g -std=c99 -Wall -Wextra -Werror -Wconversion -Wshadow \
          -Wundef -Wpointer-arith \
          -I../../firmware/ui -I../../firmware/diag -I../../firmware/lib
OUT := ../../build/ui

.PHONY: test preview clean

test: preview

preview: test_launcher
	@mkdir -p $(OUT)
	./test_launcher $(OUT)/launcher.ppm
	python3 -c "from PIL import Image; Image.open('$(OUT)/launcher.ppm').save('$(OUT)/launcher.png')"
	@echo "preview: $(OUT)/launcher.png"

test_launcher: test_launcher.c ../../firmware/ui/launcher.c \
                ../../firmware/ui/launcher_assets.c ../../firmware/ui/launcher.h \
                ../../firmware/ui/launcher_assets.h ../../firmware/diag/screen.c
	$(CC) $(CFLAGS) -o $@ test_launcher.c ../../firmware/ui/launcher.c \
	      ../../firmware/ui/launcher_assets.c ../../firmware/diag/screen.c

clean:
	rm -f test_launcher
