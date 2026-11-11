// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// Emulator Specifics for the emulator processor boards -- see emulator_specifics.h.
#include "machine/emulator_specifics.h"
#include "cpu/z80/superzazu/z80.h"
#include "cpu/z80/z80dasm.h"
#include "cpu/i8085/i8085.h"
#include "cpu/i8085/8085dasm.h"
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <sstream>

namespace {
std::string fmt(const char *f, ...) __attribute__((format(printf, 1, 2)));
std::string fmt(const char *f, ...)
{
	char b[160]; va_list ap; va_start(ap, f); vsnprintf(b, sizeof b, f, ap); va_end(ap); return b;
}
// MAME's disassemblers print "ld   hl,$0500": split into mnemonic and operands, upper case
void split(const std::string &text, std::string &mn, std::string &op)
{
	std::string s = text;
	for (char &c : s) c = char(toupper(u8(c)));
	size_t sp = s.find(' ');
	mn = s.substr(0, sp);
	op = sp == std::string::npos ? "" : s.substr(s.find_first_not_of(' ', sp));
}
}

void emulator_specifics::decode(u16 pc, const memory &mem, std::string &mnem, std::string &oper, u32 &len) const
{
	data_buffer b; b.rd = [&mem](offs_t a) -> u8 { return mem(u16(a)); };
	std::ostringstream o;
	len = disasm().disassemble(o, pc, b, b) & util::disasm_interface::LENGTHMASK;
	split(o.str(), mnem, oper);
	for (size_t i; (i = oper.find('$')) != std::string::npos;) oper.erase(i, 1);   // DOS/50 prints plain hex
}

std::string emulator_specifics::opcode(u8 op) const
{
	data_buffer b; b.rd = [op](offs_t a) -> u8 { return a == 0 ? op : 0; };
	std::ostringstream o; disasm().disassemble(o, 0, b, b);
	std::string mn, ops; split(o.str(), mn, ops);
	std::string kept;                         // the operands the opcode itself names (registers)
	std::stringstream ss(ops); std::string part;
	while (std::getline(ss, part, ',')) {
		if (part.find('$') != std::string::npos) break;
		if (!kept.empty()) kept += ',';
		kept += part;
	}
	return kept.empty() ? mn : fmt("%-6s%s", mn.c_str(), kept.c_str());
}

namespace {
// ---- Z80 emulator board (8300E04): DOS/50's Z80 trace line
class z80_specifics : public emulator_specifics
{
public:
	explicit z80_specifics(const z80 &c) : cpu(c) {}
	const char *name() const override { return "Z80"; }
	std::string trace_header() const override { return "LOC  INST     MNEM OPER        SP   F  A  B  C  D  E  H  L  IX  IY"; }
	std::string trace_line(u16 pc, const memory &mem) const override
	{
		std::string mn, op, inst; u32 len; decode(pc, mem, mn, op, len);
		for (u32 i = 0; i < len; i++) inst += fmt("%02X", mem(u16(pc + i)));
		const z80 &z = cpu;
		const u8 f = u8((z.sf << 7) | (z.zf << 6) | (z.yf << 5) | (z.hf << 4) | (z.xf << 3) | (z.pf << 2) | (z.nf << 1) | z.cf);
		return fmt("%04X %-9s%-5s%-11s%04X %02X %02X %02X %02X %02X %02X %02X %02X %04X %04X",
			pc, inst.c_str(), mn.c_str(), op.c_str(), z.sp, f, z.a, z.b, z.c, z.d, z.e, z.h, z.l, z.ix, z.iy);
	}
protected:
	util::disasm_interface &disasm() const override { return d; }
private:
	const z80 &cpu;
	mutable z80_disassembler d;
};

// ---- 8085 emulator board (8300E06): DOS/50's 8085 trace line, as its 8085 support prints it
class i8085_specifics : public emulator_specifics
{
public:
	explicit i8085_specifics(const i8085a_cpu_device &c) : cpu(c) {}
	const char *name() const override { return "8085"; }
	std::string trace_header() const override { return "LOC  INST   MNEM  OPER      SP     F   A  B  C  D  E  H  L IM SOD"; }
	std::string trace_line(u16 pc, const memory &mem) const override
	{
		std::string mn, op, inst; u32 len; decode(pc, mem, mn, op, len);
		for (u32 i = 0; i < len; i++) inst += fmt("%02X", mem(u16(pc + i)));
		const i8085a_cpu_device &p = cpu;
		return fmt("%04X %-7s%-6s%-10s%04X  %02X  %02X %02X %02X %02X %02X %02X %02X %02X  0",
			pc, inst.c_str(), mn.c_str(), op.c_str(), p.sp(), p.af() & 0xff, p.af() >> 8, p.bc() >> 8, p.bc() & 0xff,
			p.de() >> 8, p.de() & 0xff, p.hl() >> 8, p.hl() & 0xff, p.im() & 0xff);
	}
protected:
	util::disasm_interface &disasm() const override { return d; }
private:
	const i8085a_cpu_device &cpu;
	mutable i8085_disassembler d;
};
}

std::unique_ptr<emulator_specifics> make_z80_specifics(const z80 &cpu) { return std::make_unique<z80_specifics>(cpu); }
std::unique_ptr<emulator_specifics> make_i8085_specifics(const i8085a_cpu_device &cpu) { return std::make_unique<i8085_specifics>(cpu); }
