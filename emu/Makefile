CXX ?= c++
OPT ?= -O2
CXXFLAGS ?= -std=c++17 $(OPT) -g -Wall -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function
CPPFLAGS += -Isrc -MMD -MP
BUILD := build

DASM_OBJ := $(BUILD)/cpu/t11/t11dasm.o $(BUILD)/cpu/s2650/2650dasm.o $(BUILD)/cpu/z80/z80dasm.o
T11_OBJ  := $(BUILD)/cpu/t11/t11.o
M8501    := $(BUILD)/machine/tek8501.o $(BUILD)/machine/tek8501_fdc.o $(BUILD)/machine/flexdrive.o
M8301    := $(BUILD)/machine/tek8301.o $(BUILD)/machine/prom_programmer.o $(BUILD)/machine/rtpa.o $(BUILD)/machine/tta.o $(BUILD)/machine/am9513.o $(BUILD)/cpu/i8085/i8085.o $(BUILD)/cpu/s2650/s2650.o $(BUILD)/cpu/z80/superzazu/z80.o

all: $(BUILD)/dasm $(BUILD)/tek8501 $(BUILD)/tek8301 $(BUILD)/tek8550

$(BUILD)/dasm: $(BUILD)/tools/dasm.o $(DASM_OBJ)
	$(CXX) -o $@ $^
$(BUILD)/tek8501: $(BUILD)/main8501.o $(M8501) $(BUILD)/cpu/z80/superzazu/z80.o $(T11_OBJ) $(BUILD)/cpu/t11/t11dasm.o
	$(CXX) -o $@ $^

$(BUILD)/tek8301: $(BUILD)/main8301.o $(M8301) $(BUILD)/cpu/s2650/2650dasm.o
	$(CXX) -o $@ $^

SYS8550  := $(BUILD)/system/tek8550.o $(BUILD)/system/options.o $(BUILD)/host/ct8500.o $(BUILD)/host/ct8500_ansi.o $(BUILD)/host/ipc_target.o $(BUILD)/host/serial_link.o

$(BUILD)/tek8550: $(BUILD)/main8550.o $(SYS8550) $(M8301) $(M8501) $(T11_OBJ) $(DASM_OBJ)
	$(CXX) -o $@ $^

# the 8K-entry opcode table makes -O2 very slow on this file; -O1 is plenty
$(BUILD)/cpu/t11/t11.o: OPT = -O1

$(BUILD)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
$(BUILD)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) -std=c99 -O2 -g -Isrc -c $< -o $@
$(BUILD)/tools/%.o: tools/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

# optional graphical front end (SDL3 via pkg-config); never part of 'all'
SDL_CFLAGS = $(shell pkg-config --cflags sdl3 2>/dev/null)
SDL_LIBS   = $(shell pkg-config --libs sdl3 2>/dev/null)
GUI_OBJ := $(BUILD)/frontend/sdl/main_gui.o $(BUILD)/host/ct8500_raster.o
gui: $(BUILD)/tek8550-gui
$(BUILD)/tek8550-gui: $(GUI_OBJ) $(SYS8550) $(M8301) $(M8501) $(T11_OBJ) $(DASM_OBJ)
	@pkg-config --exists sdl3 || { echo "make gui needs SDL3 (pkg-config sdl3): brew install sdl3 / apt install libsdl3-dev / pacman -S mingw-w64-x86_64-SDL3"; exit 1; }
	$(CXX) -o $@ $^ $(SDL_LIBS)
$(BUILD)/frontend/sdl/main_gui.o: CPPFLAGS += $(SDL_CFLAGS)

test: $(BUILD)/flexdrive_roundtrip
	$(BUILD)/flexdrive_roundtrip
$(BUILD)/flexdrive_roundtrip: $(BUILD)/tests/flexdrive_roundtrip.o $(BUILD)/machine/flexdrive.o
	$(CXX) -o $@ $^
$(BUILD)/tests/%.o: tests/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD)
-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
.PHONY: all clean test gui
