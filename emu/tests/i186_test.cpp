// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// The 80186/80188 core (cpu/i86/i186, ported from MAME) on a flat 1 MB RAM bus: the
// 186-only instructions, the peripheral control block and its relocation, the timers
// through the interrupt controller, external interrupts, DMA and the LOCK pin.
// Run with "make test".
#include "cpu/i86/i186.h"
#include "cpu/i386/i386dasm.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <initializer_list>
#include <sstream>

static int fails = 0;
static void check(bool ok, const char *what) { printf("%s: %s\n", ok ? "ok  " : "FAIL", what); fails += !ok; }

using cpu_t = i80186_cpu_device;
enum { P = cpu_t::SP_PROGRAM, O = cpu_t::SP_OPCODES };

struct acc { char kind; int space; u32 addr; u8 data; };   // kind: r w i(n) o(ut) L(ock pin, data = state)

struct flat_bus : cpu_t::bus_interface {
	std::vector<u8> ram = std::vector<u8>(1 << 20, 0);
	u8 ports[65536] = {};
	std::vector<acc> log;
	int intas = 0; u8 vector = 0x40;
	u8 read(int space, u32 a) override { log.push_back({'r', space, a, ram[a]}); return ram[a]; }
	void write(int space, u32 a, u8 d) override { log.push_back({'w', space, a, d}); ram[a] = d; }
	u8 io_read(u16 p) override { log.push_back({'i', -1, p, ports[p]}); return ports[p]; }
	void io_write(u16 p, u8 d) override { log.push_back({'o', -1, p, d}); ports[p] = d; }
	u8 inta() override { intas++; return vector; }
	void lock(int state) override { log.push_back({'L', -1, 0, u8(state)}); }
	void load(u32 a, std::initializer_list<int> bytes) { for (int b : bytes) ram[a++ & 0xfffff] = u8(b); }
	u16 w(u32 a) const { return ram[a] | (ram[a + 1] << 8); }
	void setw(u32 a, u16 v) { ram[a] = u8(v); ram[a + 1] = u8(v >> 8); }
	bool has(char kind, u32 a) const { for (auto &x : log) if (x.kind == kind && x.addr == a) return true; return false; }
	std::vector<acc> data() const { std::vector<acc> d; for (auto &x : log) if (!(x.kind == 'r' && x.space == O)) d.push_back(x); return d; }
};

struct rig {
	flat_bus bus; cpu_t cpu{bus};
	// start at seg:0 with a stack at 2000:0100
	void start(u16 seg) { cpu.reset(); cpu.set_cs(seg); cpu.set_ip(0); cpu.set_ss(0x2000); cpu.set_sp(0x0100); cpu.set_ds(0); bus.log.clear(); }
	int go(int max = 100000) { int n = 0; while (!cpu.halted() && n < max) n += cpu.run(100); return n; }
	void vec(int n, u16 off) { bus.setw(n * 4, off); bus.setw(n * 4 + 2, 0); }   // interrupt n -> 0000:off
};

static std::string dasm(std::initializer_list<int> bytes)
{
	struct cfg : i386_disassembler::config { int get_mode() const override { return 1; } } c;
	i386_disassembler d(&c);
	std::vector<u8> b; for (int x : bytes) b.push_back(u8(x));
	data_buffer buf; buf.rd = [&](offs_t a) -> u8 { return a < b.size() ? b[a] : 0; };
	std::ostringstream s; d.disassemble(s, 0, buf, buf);
	return s.str();
}

int main()
{
	{   // reset: CS:IP FFFF:0000; the PCB is in I/O space at FF00, relocation register 20FFh
		rig r;
		r.bus.load(0xffff0, {0xea, 0x00, 0x04, 0x00, 0x00});         // jmp 0000:0400
		r.bus.load(0x00400, {0xba, 0xfe, 0xff, 0xed, 0xf4});         // mov dx,0fffeh; in ax,dx; hlt
		r.cpu.reset();
		check(r.cpu.cs() == 0xffff && r.cpu.ip() == 0 && r.cpu.relocation() == 0x20ff, "reset: CS:IP = FFFF:0000, relocation register 20FFh");
		r.go();
		check(!r.bus.log.empty() && r.bus.log[0].kind == 'r' && r.bus.log[0].space == O && r.bus.log[0].addr == 0xffff0, "reset: first access is an opcode fetch at FFFF0");
		check(r.cpu.ax() == 0x20ff && r.cpu.halted(), "in ax,0fffeh reads the relocation register (20FFh)");
		check(!r.bus.has('i', 0xfffe) && !r.bus.has('i', 0xffff), "PCB reads do not reach the bus");
	}
	{   // timing: 186 table (mov ax,imm16 = 4, push imm = 14)
		rig r; r.bus.load(0x1000, {0xb8, 0x34, 0x12, 0x68, 0x00, 0x00, 0xf4}); r.start(0x0100);
		check(r.cpu.run(1) == 4, "timing: mov ax,imm16 takes 4 cycles");
		check(r.cpu.run(1) == 14, "timing: push imm16 takes 14 cycles");
	}
	{   // PUSH imm8 (sign-extended) / imm16
		rig r; r.bus.load(0x1000, {0x6a, 0xf0, 0x68, 0x34, 0x12, 0xf4}); r.start(0x0100); r.go();
		check(r.cpu.sp() == 0xfc && r.bus.w(0x200fe) == 0xfff0 && r.bus.w(0x200fc) == 0x1234, "push -10h (6A, sign-extended), push 1234h (68)");
	}
	{   // PUSHA / POPA
		rig r;
		r.bus.load(0x1000, {0x60,                                    // pusha
		                    0x31, 0xc0, 0x31, 0xc9, 0x31, 0xd2, 0x31, 0xdb, 0x31, 0xed, 0x31, 0xf6, 0x31, 0xff,   // xor everything
		                    0x61, 0xf4});                            // popa; hlt
		r.start(0x0100);
		r.cpu.set_ax(0x1111); r.cpu.set_cx(0x2222); r.cpu.set_dx(0x3333); r.cpu.set_bx(0x4444);
		r.cpu.set_bp(0x5555); r.cpu.set_si(0x6666); r.cpu.set_di(0x7777);
		r.cpu.run(1);
		const u16 want[8] = {0x7777, 0x6666, 0x5555, 0x0100, 0x4444, 0x3333, 0x2222, 0x1111};   // DI SI BP SP BX DX CX AX upwards
		bool ok = r.cpu.sp() == 0xf0;
		for (int i = 0; i < 8; i++) ok &= r.bus.w(0x200f0 + 2 * i) == want[i];
		check(ok, "pusha: AX CX DX BX SP(original) BP SI DI");
		r.cpu.set_sp(0x00f0); r.bus.setw(0x200f6, 0xdead);           // the pushed SP is skipped by popa
		r.go();
		check(r.cpu.ax() == 0x1111 && r.cpu.cx() == 0x2222 && r.cpu.dx() == 0x3333 && r.cpu.bx() == 0x4444 && r.cpu.bp() == 0x5555
			&& r.cpu.si() == 0x6666 && r.cpu.di() == 0x7777 && r.cpu.sp() == 0x100, "popa restores all but SP, which ends at 0100h");
	}
	{   // IMUL reg,r/m,imm
		rig r;
		r.bus.load(0x1000, {0x6b, 0xc3, 0xfd,                        // imul ax,bx,-3
		                    0x69, 0xcb, 0x10, 0x00,                  // imul cx,bx,16
		                    0x69, 0x16, 0x00, 0x06, 0xff, 0x7f,      // imul dx,[600h],7fffh
		                    0xf4});
		r.start(0x0100); r.cpu.set_bx(100); r.bus.setw(0x600, 2);
		r.cpu.run(1);
		check(r.cpu.ax() == 0xfed4 && !(r.cpu.flags() & 0x801), "imul ax,bx,-3: 100*-3 = -300 (FED4h), CF OF clear");
		r.cpu.run(1);
		check(r.cpu.cx() == 1600, "imul cx,bx,16: 1600");
		r.go();
		check(r.cpu.dx() == 0xfffe && (r.cpu.flags() & 0x801) == 0x801, "imul dx,[600h],7fffh: 2*7FFFh overflows: low word FFFEh, CF OF set");
	}
	{   // ENTER / LEAVE
		rig r;
		r.bus.load(0x1000, {0xc8, 0x08, 0x00, 0x00,                  // enter 8,0
		                    0xc9,                                    // leave
		                    0xc8, 0x04, 0x00, 0x03,                  // enter 4,3
		                    0xc9, 0xf4});                            // leave; hlt
		r.start(0x0100); r.cpu.set_bp(0x0080);
		r.bus.setw(0x2007e, 0xaaaa); r.bus.setw(0x2007c, 0xbbbb);    // the caller's display at BP-2, BP-4
		r.cpu.run(1);
		check(r.cpu.bp() == 0xfe && r.cpu.sp() == 0xf6 && r.bus.w(0x200fe) == 0x0080, "enter 8,0: push bp, bp = sp, sp -= 8");
		r.cpu.run(1);
		check(r.cpu.bp() == 0x80 && r.cpu.sp() == 0x100, "leave: sp = bp, pop bp");
		r.cpu.run(1);
		// push bp (FE); frame = FE; push [bp-2], [bp-4] (FC, FA); push frame (F8); bp = FE; sp = F8-4
		check(r.cpu.bp() == 0xfe && r.cpu.sp() == 0xf4 && r.bus.w(0x200fe) == 0x0080 && r.bus.w(0x200fc) == 0xaaaa
			&& r.bus.w(0x200fa) == 0xbbbb && r.bus.w(0x200f8) == 0x00fe, "enter 4,3: display copied from the caller's frame, then the frame pointer, then locals");
		r.go();
		check(r.cpu.bp() == 0x80 && r.cpu.sp() == 0x100, "leave after a nested enter");
	}
	{   // BOUND
		rig r; r.vec(5, 0x0500);
		r.bus.load(0x0500, {0xbe, 0x55, 0x55, 0xf4});               // int 5 handler: mov si,5555h; hlt
		r.bus.load(0x1000, {0x62, 0x06, 0x00, 0x06,                  // bound ax,[600h]
		                    0xbe, 0x11, 0x11,                        // mov si,1111h
		                    0x62, 0x1e, 0x00, 0x06,                  // bound bx,[600h]
		                    0xf4});
		r.start(0x0100); r.bus.setw(0x600, 0xfff0); r.bus.setw(0x602, 0x0010);   // -16..16
		r.cpu.set_ax(0x0005); r.cpu.set_bx(0x0011);
		r.go();
		check(r.cpu.si() == 0x5555 && r.cpu.cs() == 0, "bound: 5 in -16..16 passes, 17 raises INT 5");
	}
	{   // INS / OUTS (and with REP)
		rig r;
		r.bus.ports[0x80] = 0x5a;
		r.bus.load(0x1000, {0xba, 0x80, 0x00,                        // mov dx,80h
		                    0x6c,                                    // insb
		                    0xb9, 0x03, 0x00, 0xf3, 0x6c,            // mov cx,3; rep insb
		                    0xba, 0x90, 0x00, 0xbe, 0x00, 0x07,      // mov dx,90h; mov si,700h
		                    0x6f,                                    // outsw
		                    0xb9, 0x02, 0x00, 0xf3, 0x6e,            // mov cx,2; rep outsb
		                    0xf4});
		r.start(0x0100); r.cpu.set_es(0x0080); r.cpu.set_di(0); r.bus.load(0x700, {0x11, 0x22, 0x33, 0x44});
		r.go();
		std::vector<acc> io; for (auto &x : r.bus.log) if (x.kind == 'i' || x.kind == 'o') io.push_back(x);
		check(r.bus.ram[0x800] == 0x5a && r.bus.ram[0x803] == 0x5a && r.bus.ram[0x804] == 0 && r.cpu.di() == 4 && r.cpu.cx() == 0, "insb, rep insb (CX=3): four bytes to ES:DI");
		bool ok = io.size() == 8 && io[4].kind == 'o' && io[4].addr == 0x90 && io[4].data == 0x11 && io[5].addr == 0x91 && io[5].data == 0x22
			&& io[6].addr == 0x90 && io[6].data == 0x33 && io[7].addr == 0x90 && io[7].data == 0x44 && r.cpu.si() == 0x704;
		check(ok, "outsw, rep outsb (CX=2): DS:SI to the port");
	}
	{   // shifts and rotates by an immediate count, counts masked to 5 bits
		rig r;
		r.bus.load(0x1000, {0xc1, 0xe0, 0x04,                        // shl ax,4
		                    0xc0, 0xeb, 0x02,                        // shr bl,2
		                    0xc1, 0xf9, 0x03,                        // sar cx,3
		                    0xc0, 0xc6, 0x04,                        // rol dh,4
		                    0xc0, 0xe2, 0x21,                        // shl dl,33 (= 1)
		                    0xf4});
		r.start(0x0100); r.cpu.set_ax(0x0123); r.cpu.set_bx(0x00f0); r.cpu.set_cx(0x8010); r.cpu.set_dx(0x1203);
		r.go();
		check(r.cpu.ax() == 0x1230 && (r.cpu.bx() & 0xff) == 0x3c && r.cpu.cx() == 0xf002 && r.cpu.dx() == 0x2106,
			"shl ax,4 / shr bl,2 / sar cx,3 / rol dh,4 / shl dl,33 (count & 1Fh)");
	}
	{   // undefined opcode: #UD (INT 6) on the 186
		rig r; r.vec(6, 0x0500);
		r.bus.load(0x0500, {0xbe, 0x66, 0x66, 0xf4});
		r.bus.load(0x1000, {0x0f, 0xf4});
		r.start(0x0100); r.go();
		check(r.cpu.si() == 0x6666, "opcode 0Fh raises INT 6");
	}
	{   // timer 0: max count -> interrupt controller -> vectored interrupt 8 (no INTA bus cycle)
		rig r; r.vec(8, 0x0500);
		r.bus.load(0x0500, {0xff, 0x06, 0x00, 0x06,                  // inc word [600h]
		                    0xba, 0x22, 0xff, 0xb8, 0x00, 0x80, 0xef,// mov dx,0ff22h; mov ax,8000h; out dx,ax (non-specific EOI)
		                    0xcf});                                  // iret
		r.bus.load(0x1000, {0xba, 0x52, 0xff, 0xb8, 0x64, 0x00, 0xef,// T0CMPA = 100
		                    0xba, 0x32, 0xff, 0x31, 0xc0, 0xef,      // TCUCON (timer interrupt control) = 0: unmasked, priority 0
		                    0xba, 0x56, 0xff, 0xb8, 0x01, 0xe0, 0xef,// T0CON = E001h: EN INH INT CONT
		                    0xfb,                                    // sti
		                    0xf4, 0xeb, 0xfd});                      // l: hlt; jmp l
		r.start(0x0100);
		int tout = 0; r.cpu.set_tmrout0_handler([&](int s) { tout += s; });
		r.cpu.run(40000);
		u16 n = r.bus.w(0x600);
		check(n >= 98 && n <= 100, "timer 0, max count 100, CPU clock/4: one interrupt per 400 cycles (about 100 in 40000)");
		check(r.bus.intas == 0, "internal vector: no INTA bus cycles");
		check((r.cpu.timer_control(0) & 0xe021) == 0xa021, "T0CON: EN, INT, CONT and the MC (max count) bit");
		check(tout >= 98, "TMROUT0 handler called at each max count");
		bool ok = true;
		for (int i = 0; i < 50; i++) {   // run() may finish its last instruction past the budget: it returns the cycles run
			u16 c1 = r.cpu.timer_count(0);
			int n = r.cpu.run(41 + i);
			int adv = (r.cpu.timer_count(0) - c1 + 100) % 100;
			ok &= c1 < 100 && n < 400 && adv >= n / 4 - 1 && adv <= n / 4 + 1;
		}
		check(ok, "T0CNT advances by one every 4 cycles");
	}
	{   // timer counting with the CPU kept busy (no HLT): the slice ends at the expiry
		rig r; r.vec(8, 0x0500);
		r.bus.load(0x0500, {0xff, 0x06, 0x00, 0x06, 0xba, 0x22, 0xff, 0xb8, 0x00, 0x80, 0xef, 0xcf});
		r.bus.load(0x1000, {0xba, 0x52, 0xff, 0xb8, 0xe8, 0x03, 0xef,// T0CMPA = 1000
		                    0xba, 0x32, 0xff, 0x31, 0xc0, 0xef,
		                    0xba, 0x56, 0xff, 0xb8, 0x01, 0xe0, 0xef,
		                    0xfb, 0x90, 0xeb, 0xfd});                // sti; l: nop; jmp l
		r.start(0x0100);
		for (int i = 0; i < 100; i++) r.cpu.run(4000);
		u16 n = r.bus.w(0x600);
		check(n >= 98 && n <= 100, "timer 0, max count 1000 while busy: about 100 interrupts in 400000 cycles");
	}
	{   // prescaler (P): timer 2 at max count 10 clocks timer 0 (max count 5): MC every 200 cycles
		rig r;
		r.bus.load(0x1000, {0xba, 0x62, 0xff, 0xb8, 0x0a, 0x00, 0xef,// T2CMPA = 10
		                    0xba, 0x52, 0xff, 0xb8, 0x05, 0x00, 0xef,// T0CMPA = 5
		                    0xba, 0x56, 0xff, 0xb8, 0x09, 0xc0, 0xef,// T0CON = C009h: EN INH P CONT
		                    0xba, 0x66, 0xff, 0xb8, 0x01, 0xc0, 0xef,// T2CON = C001h: EN INH CONT
		                    0xf4});
		r.start(0x0100); r.go();
		int tout = 0; r.cpu.set_tmrout0_handler([&](int s) { tout += s; });
		r.cpu.run(2000);
		check(tout == 10 && (r.cpu.timer_control(0) & 0x20), "timer 0 prescaled by timer 2: 10 max counts in 2000 cycles");
	}
	{   // external clock (EXT): TMRIN0 pulses count, max count sets MC and TMROUT0
		rig r;
		r.bus.load(0x1000, {0xba, 0x52, 0xff, 0xb8, 0x03, 0x00, 0xef,// T0CMPA = 3
		                    0xba, 0x56, 0xff, 0xb8, 0x04, 0xc0, 0xef,// T0CON = C004h: EN INH EXT
		                    0xf4});
		r.start(0x0100); r.go();
		int tout = 0; r.cpu.set_tmrout0_handler([&](int s) { tout += s; });
		for (int i = 0; i < 2; i++) { r.cpu.set_input_line(cpu_t::I80186_TMRIN0, ASSERT_LINE); r.cpu.set_input_line(cpu_t::I80186_TMRIN0, CLEAR_LINE); }
		check(r.cpu.timer_count(0) == 2 && !(r.cpu.timer_control(0) & 0x20), "TMRIN0: two pulses, count 2");
		r.cpu.set_input_line(cpu_t::I80186_TMRIN0, ASSERT_LINE);
		check((r.cpu.timer_control(0) & 0x8020) == 0x0020 && tout == 1, "third pulse: max count, MC set, TMROUT0, single run stops (EN cleared)");
	}
	{   // external INT0 (edge, unmasked) -> vector 0Ch; in cascade mode the vector comes from an INTA cycle
		rig r; r.vec(0x0c, 0x0500);
		r.bus.load(0x0500, {0x46,                                    // inc si
		                    0xba, 0x22, 0xff, 0xb8, 0x00, 0x80, 0xef, 0xcf});
		r.bus.load(0x1000, {0xba, 0x38, 0xff, 0x31, 0xc0, 0xef,      // I0CON = 0
		                    0xfb, 0xf4, 0xf4});                      // sti; hlt; hlt
		r.start(0x0100); r.cpu.set_si(0); r.go();
		r.cpu.set_input_line(cpu_t::I80186_INT0, ASSERT_LINE); r.cpu.run(200);
		r.cpu.set_input_line(cpu_t::I80186_INT0, CLEAR_LINE); r.cpu.run(200);
		check(r.cpu.si() == 1 && r.bus.intas == 0, "INT0 -> interrupt type 0Ch, internally vectored");

		r.vec(0x40, 0x0510); r.bus.load(0x0510, {0x47, 0xba, 0x22, 0xff, 0xb8, 0x00, 0x80, 0xef, 0xcf});   // inc di; EOI; iret
		r.bus.load(0x1000, {0xba, 0x38, 0xff, 0xb8, 0x20, 0x00, 0xef,// I0CON = 20h (cascade)
		                    0xfb, 0xf4, 0xf4});
		r.start(0x0100); r.cpu.set_di(0); r.go();
		r.cpu.set_input_line(cpu_t::I80186_INT0, ASSERT_LINE); r.cpu.run(200);
		check(r.cpu.di() == 1 && r.bus.intas == 1, "INT0 in cascade mode: vector 40h from bus.inta()");
	}
	{   // PCB relocation: to memory at 30000h, then back to I/O at 0400h
		rig r;
		r.bus.ports[0xfffe] = 0x99;
		r.bus.load(0x1000, {0xba, 0xfe, 0xff, 0xb8, 0x00, 0x13, 0xef,// mov dx,0fffeh; out dx,1300h: PCB in memory at 30000h
		                    0xa1, 0xfe, 0x00,                        // mov ax,[00feh]  (DS = 3000h)
		                    0x89, 0xc3,                              // mov bx,ax
		                    0xed,                                    // in ax,dx       (now the bus)
		                    0xc7, 0x06, 0xfe, 0x00, 0x04, 0x00,      // mov word [00feh],0004h: PCB in I/O at 0400h
		                    0x89, 0xc1,                              // mov cx,ax
		                    0xba, 0xfe, 0x04, 0xed,                  // mov dx,04feh; in ax,dx
		                    0xf4});
		r.start(0x0100); r.cpu.set_ds(0x3000); r.go();
		check(r.cpu.bx() == 0x1300 && !r.bus.has('r', 0x300fe), "relocated to memory: [300FEh] reads the relocation register, not RAM");
		check((r.cpu.cx() & 0xff) == 0x99 && r.bus.has('i', 0xfffe), "I/O FFFEh then goes to the bus");
		check(r.cpu.ax() == 0x0004 && r.cpu.relocation() == 0x0004 && !r.bus.has('w', 0x300fe) && !r.bus.has('i', 0x04fe), "memory write moves it to I/O 0400h; in ax,04feh reads it");
	}
	{   // DMA channel 0: unsynchronized memory to memory (whole block at once), then source-synchronized on DRQ0
		rig r;
		r.bus.load(0x4000, {1, 2, 3, 4, 5, 6});
		r.bus.load(0x1000, {0xba, 0xc0, 0xff, 0xb8, 0x00, 0x40, 0xef,// D0SRC = 04000h
		                    0xba, 0xc2, 0xff, 0x31, 0xc0, 0xef,
		                    0xba, 0xc4, 0xff, 0xb8, 0x00, 0x50, 0xef,// D0DST = 05000h
		                    0xba, 0xc6, 0xff, 0x31, 0xc0, 0xef,
		                    0xba, 0xc8, 0xff, 0xb8, 0x04, 0x00, 0xef,// D0TC = 4
		                    0xba, 0xca, 0xff, 0xb8, 0x06, 0xb6, 0xef,// D0CON = B606h: mem->mem, both increment, TC, CHG, ST, bytes
		                    0xf4});
		r.start(0x0100); r.go();
		check(r.bus.ram[0x5000] == 1 && r.bus.ram[0x5003] == 4 && r.bus.ram[0x5004] == 0, "unsynchronized DMA: 4 bytes copied");
		r.bus.load(0x1000, {0xba, 0xc8, 0xff, 0xb8, 0x02, 0x00, 0xef,// D0TC = 2 (source/dest continue at 4004h/5004h)
		                    0xba, 0xca, 0xff, 0xb8, 0x46, 0xb6, 0xef,// D0CON = B646h: source synchronized
		                    0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0xf4});
		r.start(0x0100); r.cpu.run(30);
		check(r.bus.ram[0x5004] == 0, "source-synchronized DMA waits for DRQ0");
		r.cpu.set_input_line(cpu_t::I80186_DRQ0, ASSERT_LINE); r.go();
		check(r.bus.ram[0x5004] == 5 && r.bus.ram[0x5005] == 6 && r.bus.ram[0x5006] == 0, "DRQ0: 2 more bytes, then terminal count stops the channel");
	}
	{   // LOCK pin on the 186 core, as on the 8086
		rig r;
		r.bus.load(0x1000, {0xb0, 0x5a,                              // mov al,5ah
		                    0xf0, 0x26, 0x88, 0x07,                  // lock es: mov [bx],al
		                    0x88, 0x07,                              // mov [bx],al        (not locked)
		                    0xf0, 0x60,                              // lock pusha
		                    0xf4});
		r.start(0x0100); r.cpu.set_ds(0x0800); r.cpu.set_es(0x0900); r.cpu.set_bx(0x10);
		bool during = false;
		r.cpu.trace_hook = [&](u32) { during |= r.cpu.locked(); };
		r.go();
		auto d = r.bus.data();
		auto is = [&](size_t i, char k, u32 a) { return i < d.size() && d[i].kind == k && d[i].addr == a; };
		auto lk = [&](size_t i, int st) { return i < d.size() && d[i].kind == 'L' && d[i].data == st; };
		check(lk(0, 1) && is(1, 'w', 0x9010) && d[1].space == P && lk(2, 0), "lock es: mov [bx],al: lock(1), write to ES:BX, lock(0)");
		check(is(3, 'w', 0x8010), "the next mov is not locked");
		check(lk(4, 1) && d.size() == 22 && lk(21, 0), "lock pusha: one lock(1)/lock(0) around the sixteen stack writes");
		check(!during && !r.cpu.locked(), "LOCK is released at every instruction boundary");
	}
	{   // 80188
		flat_bus bus; i80188_cpu_device cpu(bus);
		bus.load(0x1000, {0x68, 0x34, 0x12, 0xf4});
		cpu.reset(); cpu.set_cs(0x0100); cpu.set_ip(0); cpu.set_ss(0x2000); cpu.set_sp(0x100); cpu.run(100);
		check(cpu.is_80188() && cpu.halted() && bus.w(0x200fe) == 0x1234, "80188: runs push imm16");
	}
	{   // disassembler: the 186 opcodes in 16-bit mode
		check(dasm({0x6a, 0xf0}) == "push    -10h", "dasm: push imm8");
		check(dasm({0x60}) == "pusha   ", "dasm: pusha");
		check(dasm({0xc8, 0x08, 0x00, 0x00}) == "enter   8h,0h", "dasm: enter");
		check(dasm({0xc1, 0xe0, 0x04}) == "shl     ax,4h", "dasm: shl ax,4");
		check(dasm({0x62, 0x06, 0x00, 0x06}) == "bound   ax,[600h]", "dasm: bound");
		check(dasm({0x6f}) == "outsw   ", "dasm: outsw");
		check(dasm({0x6b, 0xc3, 0xfd}) == "imul    ax,bx,-3h", "dasm: imul ax,bx,-3");
	}
	printf("%s: i186 core, %d failure(s)\n", fails ? "FAIL" : "ok  ", fails);
	return fails != 0;
}
