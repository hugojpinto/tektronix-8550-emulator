// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// What the rest of the lab needs to know about the processor on the active emulator board:
// the Tektronix manuals' "Emulator Specifics" (each emulator's own chapter of the system
// users manual). One implementation per emulator processor board; the 8301 hands out the
// active one (tek8301::emulator()). Software models that print the program's state (the TTA
// commands' trace lines and disassembly) use this instead of knowing the processor.
#ifndef TEK_MACHINE_EMULATOR_SPECIFICS_H
#define TEK_MACHINE_EMULATOR_SPECIFICS_H
#include "emu.h"
#include <functional>
#include <memory>
#include <string>

struct z80;
class i8085a_cpu_device;

class emulator_specifics
{
public:
	using memory = std::function<u8(u16)>;    // program memory as the processor sees it, no side effects
	virtual ~emulator_specifics() = default;
	virtual const char *name() const = 0;     // as SEL takes it: "Z80", "8085"
	virtual double clock_hz() const = 0;      // the processor clock (the TTA's time base in run mode)
	// the instruction at pc, decoded with its operands: mnemonic and operand fields
	void decode(u16 pc, const memory &mem, std::string &mnem, std::string &oper, u32 &len) const;
	// an opcode byte decoded alone, as an acquisition display does (operands that come from
	// later bytes are not shown): "LXI   H", "ADD   A,(HL)", "JNZ"
	std::string opcode(u8 op) const;
	// DOS/50's trace line for this processor, after the instruction at pc has run
	virtual std::string trace_header() const = 0;
	virtual std::string trace_line(u16 pc, const memory &mem) const = 0;
protected:
	virtual util::disasm_interface &disasm() const = 0;
};

std::unique_ptr<emulator_specifics> make_z80_specifics(const z80 &cpu);
std::unique_ptr<emulator_specifics> make_i8085_specifics(const i8085a_cpu_device &cpu);
#endif
