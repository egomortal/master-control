# The console payload, built with ps5-payload-sdk. Called from Makefile.

include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

VERSION := $(shell sed -n 's/^\#define PADBRIDGE_VERSION "\(.*\)"/\1/p' src/version.h)
ELF   := dist/master-control.elf
BUILD := build/ps5

CFLAGS  := -std=c11 -Wall -Wextra -O2 -Isrc
LDLIBS  += -lpthread -ldl    # system libraries: loaded at run time by src/ps5_libs.c

SRCS := src/util.c src/log.c src/crc32.c src/smp_crypto.c src/profiles.c src/generic.c \
        src/host.c src/le.c \
        src/web.c src/web_page.c src/web_classic.c src/i18n.c src/remap.c \
        src/config.c src/evstream.c src/hotkey.c src/lock.c src/instance.c src/netinfo.c src/usb_desc.c src/suspend.c \
        src/hci_usb.c src/ps5_power.c src/ps5_ui.c src/ps5_apps.c src/ps5_sysinfo.c src/launcher.c src/icon_data.c src/ps5_vpad.c src/xinput.c src/ps5_wired.c src/ps5_libs.c src/gc_usb.c src/hubcard.c src/web_overlay.c src/ps5_main.c
OBJS := $(patsubst src/%.c,$(BUILD)/%.o,$(SRCS))

$(ELF): $(OBJS)
	@mkdir -p dist
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

dist/PadBridge-PS5-icon-installer.elf: tools/install-icon-ps5.c src/launcher.c src/hubcard.c src/icon_data.c
	@mkdir -p dist
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -ldl

$(BUILD)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<
