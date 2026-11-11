# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
# Tektronix 8550 emulator.
#   make                       native build in build/ (macOS, Linux, or Windows under MSYS2/MinGW)
#   make gui                   + build/tek8550-gui (SDL3, found with pkg-config)
#   make test                  unit tests
#   make CROSS=x86_64-w64-mingw32-            Windows .exe files from macOS/Linux, in build-win/
#   make gui CROSS=x86_64-w64-mingw32- SDL3_DIR=/path/to/SDL3-x.y.z/x86_64-w64-mingw32
#                              (the official SDL3-devel-*-mingw package; SDL3.dll is copied next to the .exe)
OPT ?= -O2
CXXFLAGS ?= -std=c++17 $(OPT) -g -Wall -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function
CPPFLAGS += -Isrc -MMD -MP

ifdef CROSS
CXX := $(CROSS)g++
CC  := $(CROSS)gcc
TARGET_OS := windows
BUILD ?= build-win
endif
ifeq ($(OS),Windows_NT)
TARGET_OS := windows
endif
ifeq ($(TARGET_OS),windows)
EXE := .exe
LDLIBS += -lws2_32
# self-contained executables: no MinGW runtime DLLs needed next to them
LDFLAGS += -static -static-libgcc -static-libstdc++
endif
BUILD ?= build

DASM_OBJ := $(BUILD)/cpu/t11/t11dasm.o $(BUILD)/cpu/s2650/2650dasm.o $(BUILD)/cpu/z80/z80dasm.o $(BUILD)/cpu/i8085/8085dasm.o $(BUILD)/cpu/i386/i386dasm.o
I86_OBJ  := $(BUILD)/cpu/i86/i86.o $(BUILD)/cpu/i86/i186.o
T11_OBJ  := $(BUILD)/cpu/t11/t11.o
M8501    := $(BUILD)/machine/tek8501.o $(BUILD)/machine/tek8501_fdc.o $(BUILD)/machine/flexdrive.o
M8301    := $(BUILD)/machine/tek8301.o $(BUILD)/machine/prom_programmer.o $(BUILD)/machine/rtpa.o $(BUILD)/machine/tta.o $(BUILD)/machine/tta_run.o $(BUILD)/machine/emulator_specifics.o $(BUILD)/machine/ep8085.o $(BUILD)/machine/ep8086.o $(I86_OBJ) $(BUILD)/machine/z80_slave.o $(BUILD)/machine/epz80.o $(BUILD)/machine/am9513.o $(BUILD)/cpu/i8085/i8085.o $(BUILD)/cpu/s2650/s2650.o $(BUILD)/cpu/z80/superzazu/z80.o
SYS8550  := $(BUILD)/system/tek8550.o $(BUILD)/system/tta_console.o $(BUILD)/system/options.o $(BUILD)/host/ct8500.o $(BUILD)/host/ct8500_ansi.o $(BUILD)/host/ipc_target.o $(BUILD)/host/serial_link.o $(BUILD)/host/net.o

all: $(BUILD)/dasm$(EXE) $(BUILD)/tek8501$(EXE) $(BUILD)/tek8301$(EXE) $(BUILD)/tek8550$(EXE)

$(BUILD)/dasm$(EXE): $(BUILD)/tools/dasm.o $(DASM_OBJ)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)
$(BUILD)/tek8501$(EXE): $(BUILD)/main8501.o $(M8501) $(BUILD)/cpu/z80/superzazu/z80.o $(T11_OBJ) $(BUILD)/cpu/t11/t11dasm.o
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)
$(BUILD)/tek8301$(EXE): $(BUILD)/main8301.o $(M8301) $(DASM_OBJ)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)
$(BUILD)/tek8550$(EXE): $(BUILD)/main8550.o $(SYS8550) $(M8301) $(M8501) $(T11_OBJ) $(DASM_OBJ)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# the 8K-entry opcode table makes -O2 very slow on this file; -O1 is plenty
$(BUILD)/cpu/t11/t11.o: OPT = -O1
# MAME's x86 disassembler compares offs_t with int (gcc's -Wall warns; MAME builds without it)
$(BUILD)/cpu/i386/i386dasm.o: CXXFLAGS += -Wno-sign-compare

$(BUILD)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
$(BUILD)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) -std=c99 -O2 -g -Isrc -c $< -o $@
$(BUILD)/tools/%.o: tools/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

# optional graphical front end (SDL3); never part of 'all'.
# SDL3 from pkg-config, or from SDL3_DIR (include/ lib/ bin/ of an SDL3 development package)
ifdef SDL3_DIR
SDL_CFLAGS = -I$(SDL3_DIR)/include
SDL_LIBS   = -L$(SDL3_DIR)/lib -lSDL3
SDL_CHECK  = test -f $(SDL3_DIR)/include/SDL3/SDL.h || { echo "SDL3_DIR=$(SDL3_DIR) has no include/SDL3/SDL.h"; exit 1; }
else
SDL_CFLAGS = $(shell pkg-config --cflags sdl3 2>/dev/null)
SDL_LIBS   = $(shell pkg-config --libs sdl3 2>/dev/null)
SDL_CHECK  = pkg-config --exists sdl3 || { echo "make gui needs SDL3: brew install sdl3 / apt install libsdl3-dev / pacman -S mingw-w64-x86_64-SDL3, or SDL3_DIR=..."; exit 1; }
endif
# SDL itself is a DLL: link it dynamically, everything else statically (Windows: SDL3.dll is the
# only file needed next to the .exe)
# (SDL3_DIR on Windows: -static stays, and SDL3's import library is named by path, which -static allows)
ifeq ($(TARGET_OS),windows)
ifdef SDL3_DIR
SDL_LIBS    = $(SDL3_DIR)/lib/libSDL3.dll.a
GUI_LDFLAGS = $(LDFLAGS)
else
GUI_LDFLAGS = $(filter-out -static,$(LDFLAGS))
endif
else
GUI_LDFLAGS = $(LDFLAGS)
endif
GUI_OBJ := $(BUILD)/frontend/sdl/main_gui.o $(BUILD)/frontend/sdl/layout.o $(BUILD)/host/ct8500_raster.o
gui: $(BUILD)/tek8550-gui$(EXE)
$(BUILD)/tek8550-gui$(EXE): $(GUI_OBJ) $(SYS8550) $(M8301) $(M8501) $(T11_OBJ) $(DASM_OBJ)
	@$(SDL_CHECK)
	$(CXX) $(GUI_LDFLAGS) -o $@ $^ $(SDL_LIBS) $(LDLIBS)
ifeq ($(TARGET_OS),windows)
ifdef SDL3_DIR
	cp $(SDL3_DIR)/bin/SDL3.dll $(BUILD)/
endif
endif
$(GUI_OBJ): CPPFLAGS += $(SDL_CFLAGS)

test: $(BUILD)/flexdrive_roundtrip$(EXE) $(BUILD)/ct8500_keys$(EXE) $(BUILD)/i86_test$(EXE) $(BUILD)/i186_test$(EXE)
	$(BUILD)/flexdrive_roundtrip$(EXE) $(wildcard ../sources/bitsavers/disks/*.imd ../sources/bitsavers/disks/*.IMD)
	$(BUILD)/ct8500_keys$(EXE)
	$(BUILD)/i86_test$(EXE)
	$(BUILD)/i186_test$(EXE)
$(BUILD)/ct8500_keys$(EXE): $(BUILD)/tests/ct8500_keys.o $(BUILD)/host/ct8500.o
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)
$(BUILD)/i86_test$(EXE): $(BUILD)/tests/i86_test.o $(I86_OBJ) $(BUILD)/cpu/i386/i386dasm.o
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)
$(BUILD)/i186_test$(EXE): $(BUILD)/tests/i186_test.o $(I86_OBJ) $(BUILD)/cpu/i386/i386dasm.o
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)
$(BUILD)/flexdrive_roundtrip$(EXE): $(BUILD)/tests/flexdrive_roundtrip.o $(BUILD)/machine/flexdrive.o
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)
$(BUILD)/tests/%.o: tests/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD)
-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
.PHONY: all clean test gui
