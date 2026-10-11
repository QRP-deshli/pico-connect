# Makefile for pico-connect
#
#   make            build for whatever platform you're running this on
#   make linux      force a native POSIX build (gcc + pthread)
#   make windows    cross-compile pico-connect.exe with MinGW-w64
#                   (on Linux/macOS: needs the x86_64-w64-mingw32-gcc
#                   package; on Windows itself, just uses your normal gcc)
#   make clean      remove built binaries and the build/ directory
#
# Layout:
#   src/       .c files
#   include/   .h files
#   build/     object files (created automatically, one subfolder per target)

# Note: plain `?=` wouldn't work here -- Make ships its own built-in
# default of CC=cc, so `?=` sees CC as "already set" and does nothing.
# `:=` overrides that built-in, while still letting `make CC=clang`
# on the command line win over this (command-line assignments always
# take priority over anything in the makefile itself).
CC         := gcc
WINDOWS_CC ?= x86_64-w64-mingw32-gcc

SRC_DIR    := src
INC_DIR    := include
BUILD_DIR  := build

SRCS       := $(wildcard $(SRC_DIR)/*.c)
WARNINGS   := -Wall -Wextra -std=c11
CPPFLAGS   := -I$(INC_DIR) -MMD -MP

LINUX_OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/linux/%.o,$(SRCS))
WIN_OBJS   := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/windows/%.o,$(SRCS))

ifeq ($(OS),Windows_NT)
    NATIVE_TARGET := windows
    WIN_CC        := $(CC)
else
    NATIVE_TARGET := linux
    WIN_CC        := $(WINDOWS_CC)
endif

.PHONY: all linux windows clean

all: $(NATIVE_TARGET)

linux: pico-connect

windows: pico-connect.exe

pico-connect: $(LINUX_OBJS)
	$(CC) $(WARNINGS) -pthread -o $@ $^

pico-connect.exe: $(WIN_OBJS)
	$(WIN_CC) $(WARNINGS) -o $@ $^ -lsetupapi

$(BUILD_DIR)/linux/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(WARNINGS) -pthread -c $< -o $@

$(BUILD_DIR)/windows/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(WIN_CC) $(CPPFLAGS) $(WARNINGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) pico-connect pico-connect.exe

-include $(LINUX_OBJS:.o=.d) $(WIN_OBJS:.o=.d)
