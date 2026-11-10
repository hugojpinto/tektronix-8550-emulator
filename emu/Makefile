CXX ?= c++
OPT ?= -O2
CXXFLAGS ?= -std=c++17 $(OPT) -g -Wall -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function
CPPFLAGS += -Isrc -MMD -MP
BUILD := build

DASM_OBJ := $(BUILD)/cpu/t11/t11dasm.o $(BUILD)/cpu/s2650/2650dasm.o $(BUILD)/cpu/z80/z80dasm.o
T11_OBJ  := $(BUILD)/cpu/t11/t11.o
M8501    := $(BUILD)/machine/tek8501.o
M8301    := $(BUILD)/machine/tek8301.o $(BUILD)/cpu/s2650/s2650.o

all: $(BUILD)/dasm $(BUILD)/tek8501 $(BUILD)/tek8301 $(BUILD)/tek8550

$(BUILD)/dasm: $(BUILD)/tools/dasm.o $(DASM_OBJ)
	$(CXX) -o $@ $^
$(BUILD)/tek8501: $(BUILD)/main8501.o $(M8501) $(T11_OBJ) $(BUILD)/cpu/t11/t11dasm.o
	$(CXX) -o $@ $^

$(BUILD)/tek8301: $(BUILD)/main8301.o $(M8301) $(BUILD)/cpu/s2650/2650dasm.o
	$(CXX) -o $@ $^

$(BUILD)/tek8550: $(BUILD)/main8550.o $(M8301) $(M8501) $(T11_OBJ) $(DASM_OBJ)
	$(CXX) -o $@ $^

# the 8K-entry opcode table makes -O2 very slow on this file; -O1 is plenty
$(BUILD)/cpu/t11/t11.o: OPT = -O1

$(BUILD)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
$(BUILD)/tools/%.o: tools/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD)
-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
.PHONY: all clean
