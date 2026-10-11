# Makefile for pico-connect
#
#   make            build for whatever platform you're running this on
#   make linux      force a native POSIX build (gcc + pthread)
#   make windows    cross-compile pico-connect.exe with MinGW-w64
#                   (on Linux/macOS: needs the x86_64-w64-mingw32-gcc
#                   package; on Windows itself, just uses your normal gcc)
#   make clean      remove built binaries and the build/ directory
#
# On Windows this works from PowerShell/cmd (needs gcc and make on PATH,
# e.g. MinGW-w64) as well as from an MSYS2 shell.
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

# Shell commands that differ between POSIX shells and cmd.exe.
MKDIR_BUILD = mkdir -p $(@D)
RM_BUILD    = rm -rf $(BUILD_DIR)
RM_LINUX    = rm -f pico-connect
RM_WINDOWS  = rm -f pico-connect.exe

ifeq ($(OS),Windows_NT)
    NATIVE_TARGET := windows
    WIN_CC        := $(CC)
    # Outside an MSYS2 / Git Bash shell (plain PowerShell or cmd) there is
    # no sh, mkdir -p or rm, so use the cmd.exe equivalents instead.
    ifeq ($(MSYSTEM),)
        SHELL        := cmd.exe
        MKDIR_BUILD   = if not exist "$(subst /,\,$(@D))" mkdir "$(subst /,\,$(@D))"
        RM_BUILD      = if exist $(BUILD_DIR) rmdir /s /q $(BUILD_DIR)
        RM_LINUX      = if exist pico-connect del /q pico-connect
        RM_WINDOWS    = if exist pico-connect.exe del /q pico-connect.exe
    endif
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
	@$(MKDIR_BUILD)
	$(CC) $(CPPFLAGS) $(WARNINGS) -pthread -c $< -o $@

$(BUILD_DIR)/windows/%.o: $(SRC_DIR)/%.c
	@$(MKDIR_BUILD)
	$(WIN_CC) $(CPPFLAGS) $(WARNINGS) -c $< -o $@

clean:
	$(RM_BUILD)
	$(RM_LINUX)
	$(RM_WINDOWS)

-include $(LINUX_OBJS:.o=.d) $(WIN_OBJS:.o=.d)
