// dasm: disassemble a raw binary for pdp11 | s2650 | z80.
// usage: dasm <cpu> <file> [base(hex, or 0o-prefixed octal)] [start] [count]
#include "emu.h"
#include "cpu/t11/t11dasm.h"
#include "cpu/s2650/2650dasm.h"
#include "cpu/z80/z80dasm.h"
#include <vector>
#include <sstream>
#include <iostream>
#include <memory>
#include <cstring>
struct s2650cfg : s2650_disassembler::config { bool get_z80_mnemonics_mode() const override { return false; } };
static u32 num(const char *s) { return !strncmp(s, "0o", 2) ? strtoul(s + 2, 0, 8) : strtoul(s, 0, 16); }
int main(int argc, char **argv) {
	if (argc < 3) { fprintf(stderr, "usage: dasm pdp11|s2650|z80 file [base] [start] [count]\n"); return 1; }
	std::string cpu = argv[1];
	FILE *f = fopen(argv[2], "rb"); if (!f) { perror(argv[2]); return 1; }
	std::vector<u8> img; int c; while ((c = fgetc(f)) != EOF) img.push_back(u8(c)); fclose(f);
	u32 base = argc > 3 ? num(argv[3]) : 0, pc = argc > 4 ? num(argv[4]) : base;
	u32 end = argc > 5 ? pc + num(argv[5]) : base + img.size();
	static s2650cfg cfg;
	std::unique_ptr<util::disasm_interface> d;
	bool octal = cpu == "pdp11";
	if (octal) d = std::make_unique<t11_disassembler>();
	else if (cpu == "s2650") d = std::make_unique<s2650_disassembler>(&cfg);
	else d = std::make_unique<z80_disassembler>();
	data_buffer buf; buf.rd = [&](offs_t a) -> u8 { a -= base; return a < img.size() ? img[a] : 0xff; };
	while (pc < end) {
		std::ostringstream s; u32 r = d->disassemble(s, pc, buf, buf); u32 len = r & util::disasm_interface::LENGTHMASK; if (!len) len = 1;
		std::string hex;
		if (octal) { for (u32 i = 0; i < len; i += 2) hex += util::string_format("%06o ", buf.r16(pc + i)); printf("%06o: %-21s %s\n", pc, hex.c_str(), s.str().c_str()); }
		else { for (u32 i = 0; i < len; i++) hex += util::string_format("%02x ", buf.r8(pc + i)); printf("%04x: %-12s %s\n", pc, hex.c_str(), s.str().c_str()); }
		pc += len;
	}
}
