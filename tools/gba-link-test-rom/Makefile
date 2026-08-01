# SPDX-License-Identifier: CC0-1.0
CROSS ?= arm-none-eabi-
CC := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
PYTHON ?= python3

CFLAGS := -mcpu=arm7tdmi -marm -Os -ffreestanding -fno-builtin \
	-fomit-frame-pointer -ffunction-sections -fdata-sections \
	-Wall -Wextra -Werror
LDFLAGS := -nostdlib -Wl,--gc-sections -Wl,-T,linker.ld

.PHONY: all clean verify-fixture update-fixture

all: build/gba-link-test.gba build/gba-link-continuous.gba

build:
	mkdir -p $@

build/header.o: header.s | build
	$(CC) $(CFLAGS) -c $< -o $@

build/main.o: main.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/continuous-main.o: main.c | build
	$(CC) $(CFLAGS) -DGBA_LINK_CONTINUOUS -c $< -o $@

build/gba-link-test.elf: build/header.o build/main.o linker.ld
	$(CC) $(CFLAGS) $(LDFLAGS) build/header.o build/main.o -o $@

build/gba-link-test.gba: build/gba-link-test.elf fix-header.py
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) fix-header.py $@

build/gba-link-continuous.elf: build/header.o build/continuous-main.o linker.ld
	$(CC) $(CFLAGS) $(LDFLAGS) build/header.o build/continuous-main.o -o $@

build/gba-link-continuous.gba: build/gba-link-continuous.elf fix-header.py
	$(OBJCOPY) -O binary $< $@
	$(PYTHON) fix-header.py $@

verify-fixture: build/gba-link-test.gba build/gba-link-continuous.gba
	cmp build/gba-link-test.gba fixtures/gba-link-test.gba
	cmp build/gba-link-continuous.gba fixtures/gba-link-continuous.gba

update-fixture: build/gba-link-test.gba build/gba-link-continuous.gba
	cp build/gba-link-test.gba fixtures/gba-link-test.gba
	cp build/gba-link-continuous.gba fixtures/gba-link-continuous.gba

clean:
	rm -f build/header.o build/main.o build/continuous-main.o \
		build/gba-link-test.elf build/gba-link-test.gba \
		build/gba-link-continuous.elf build/gba-link-continuous.gba
