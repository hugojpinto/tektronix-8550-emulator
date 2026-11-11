// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// The 8086 core (cpu/i86, ported from MAME) and its disassembler, on a flat 1 MB RAM bus:
// small hand-assembled programs, checked for results, flags and which bus space each
// access goes to. Run with "make test".
#include "cpu/i86/i86.h"
#include "cpu/i386/i386dasm.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <initializer_list>
#include <sstream>

static int fails = 0;
static void check(bool ok, const char *what) { printf("%s: %s\n", ok ? "ok  " : "FAIL", what); fails += !ok; }

using cpu_t = i8086_cpu_device;
enum { P = cpu_t::SP_PROGRAM, O = cpu_t::SP_OPCODES, S = cpu_t::SP_STACK, C = cpu_t::SP_CODE, E = cpu_t::SP_EXTRA };

struct acc { char kind; int space; u32 addr; u8 data; };   // kind: r w i(n) o(ut) L(ock pin, data = state)

struct flat_bus : cpu_t::bus_interface {
	std::vector<u8> ram = std::vector<u8>(1 << 20, 0);
	u8 ports[65536] = {};
	std::vector<acc> log;
	std::vector<int> locks;
	int intas = 0; u8 vector = 0x40;
	u8 read(int space, u32 a) override { log.push_back({'r', space, a, ram[a]}); return ram[a]; }
	void write(int space, u32 a, u8 d) override { log.push_back({'w', space, a, d}); ram[a] = d; }
	u8 io_read(u16 p) override { log.push_back({'i', -1, p, ports[p]}); return ports[p]; }
	void io_write(u16 p, u8 d) override { log.push_back({'o', -1, p, d}); ports[p] = d; }
	u8 inta() override { intas++; return vector; }
	void lock(int state) override { locks.push_back(state); log.push_back({'L', -1, 0, u8(state)}); }
	void load(u32 a, std::initializer_list<int> bytes) { for (int b : bytes) ram[a++ & 0xfffff] = u8(b); }
	u16 w(u32 a) const { return ram[a] | (ram[a + 1] << 8); }
	bool has(char kind, int space, u32 a) const { for (auto &x : log) if (x.kind == kind && x.space == space && x.addr == a) return true; return false; }
};

struct rig {
	flat_bus bus; cpu_t cpu{bus};
	// start at seg:0 with a stack at 2000:0100
	void start(u16 seg) { cpu.reset(); cpu.set_cs(seg); cpu.set_ip(0); cpu.set_ss(0x2000); cpu.set_sp(0x0100); }
	int go(int max = 100000) { int n = 0; while (!cpu.halted() && n < max) n += cpu.run(100); return n; }
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
	{   // reset: CS=FFFF IP=0, the first fetch is at FFFF0
		rig r;
		r.bus.load(0xffff0, {0xea, 0x00, 0x04, 0x00, 0x00});   // jmp 0000:0400
		r.bus.load(0x00400, {0xb8, 0x34, 0x12, 0xf4});         // mov ax,1234h; hlt
		r.cpu.reset();
		check(r.cpu.cs() == 0xffff && r.cpu.ip() == 0 && r.cpu.pc() == 0xffff0, "reset: CS:IP = FFFF:0000 (pc FFFF0)");
		r.go();
		check(!r.bus.log.empty() && r.bus.log[0].kind == 'r' && r.bus.log[0].space == O && r.bus.log[0].addr == 0xffff0, "reset: first access is an opcode fetch at FFFF0");
		check(r.cpu.ax() == 0x1234 && r.cpu.cs() == 0 && r.cpu.ip() == 0x404 && r.cpu.halted(), "reset: far jump to 0000:0400, mov, hlt");
	}
	{   // timing: mov ax,imm16 is 4 clocks on the 8086 (MAME's table)
		rig r; r.bus.load(0x1000, {0xb8, 0x34, 0x12, 0xf4}); r.start(0x0100);
		check(r.cpu.run(1) == 4 && r.cpu.ax() == 0x1234, "timing: mov ax,imm16 takes 4 cycles");
	}
	{   // arithmetic and flags
		rig r;
		r.bus.load(0x1000, {0xb0, 0xff, 0x04, 0x01, 0xf4});    // mov al,0ffh; add al,1; hlt
		r.start(0x0100); r.go();
		check(r.cpu.ax() == 0 && (r.cpu.flags() & 0x8d5) == 0x055, "add al: 0ffh+1 = 0, CF ZF AF PF set, OF SF clear");
		check((r.cpu.flags() & 0xf002) == 0xf002, "flags: 8086 reads bits 15-12 and 1 as ones");
		r.bus.load(0x1000, {0xb8, 0xff, 0x7f, 0x05, 0x01, 0x00, 0xf4});   // mov ax,7fffh; add ax,1; hlt
		r.start(0x0100); r.go();
		check(r.cpu.ax() == 0x8000 && (r.cpu.flags() & 0x8d5) == 0x894, "add ax: 7fffh+1 = 8000h, OF SF AF PF set, CF ZF clear");
		r.bus.load(0x1000, {0xb0, 0x15, 0x2c, 0x27, 0x2f, 0xf4});   // mov al,15h; sub al,27h; das; hlt
		r.start(0x0100); r.go();
		check((r.cpu.ax() & 0xff) == 0x88 && (r.cpu.flags() & 1), "sub/das: 15 - 27 = 88 with borrow (BCD)");
		r.cpu.set_flags(0x0001); check((r.cpu.flags() & 0x0fd5) == 0x0001, "set_flags/flags round trip");
	}
	{   // far call and far return; pushes go to the stack space
		rig r;
		r.bus.load(0x1000, {0x9a, 0x00, 0x00, 0x00, 0x03, 0xf4});   // call 0300:0000; hlt
		r.bus.load(0x3000, {0xbb, 0x55, 0x55, 0xcb});               // mov bx,5555h; retf
		r.start(0x0100); r.go();
		check(r.cpu.bx() == 0x5555 && r.cpu.cs() == 0x0100 && r.cpu.ip() == 6 && r.cpu.sp() == 0x100, "call far / retf");
		check(r.bus.w(0x200fe) == 0x0100 && r.bus.w(0x200fc) == 0x0005, "call far pushed CS then IP");
		check(r.bus.has('w', S, 0x200fe) && r.bus.has('w', S, 0x200fc) && r.bus.has('r', S, 0x200fc), "call far / retf: stack accesses are SP_STACK");
	}
	{   // near call / ret, loop
		rig r;
		r.bus.load(0x1000, {0xb9, 0x05, 0x00, 0x31, 0xc0, 0xe8, 0x03, 0x00, 0xe2, 0xfb, 0xf4, 0x40, 0xc3});
		// mov cx,5; xor ax,ax; l: call inc_ax; loop l; hlt; inc_ax: inc ax; ret
		r.start(0x0100); r.go();
		check(r.cpu.ax() == 5 && r.cpu.cx() == 0 && r.cpu.sp() == 0x100, "call near / ret / loop");
	}
	{   // rep movsb
		rig r;
		r.bus.load(0x4000, {'H', 'E', 'L', 'L', 'O'});
		r.bus.load(0x1000, {0xfc, 0xf3, 0xa4, 0xf4});               // cld; rep movsb; hlt
		r.start(0x0100); r.cpu.set_ds(0x0400); r.cpu.set_es(0x0500); r.cpu.set_si(0); r.cpu.set_di(0); r.cpu.set_cx(5);
		r.go();
		check(!memcmp(&r.bus.ram[0x5000], "HELLO", 5) && r.cpu.cx() == 0 && r.cpu.si() == 5 && r.cpu.di() == 5, "rep movsb copies 5 bytes");
		check(r.bus.has('r', P, 0x4000) && r.bus.has('w', E, 0x5004), "movsb reads DS:SI (SP_PROGRAM), writes ES:DI (SP_EXTRA)");
	}
	{   // rep movsb interrupted by the end of the cycle budget restarts and finishes
		rig r;
		for (int i = 0; i < 200; i++) r.bus.ram[0x4000 + i] = u8(i + 1);
		r.bus.load(0x1000, {0xfc, 0xf3, 0xa4, 0xf4});
		r.start(0x0100); r.cpu.set_ds(0x0400); r.cpu.set_es(0x0500); r.cpu.set_cx(200);
		while (!r.cpu.halted()) r.cpu.run(20);
		check(!memcmp(&r.bus.ram[0x5000], &r.bus.ram[0x4000], 200) && r.cpu.cx() == 0, "rep movsb across run() calls");
	}
	{   // mul / div / divide error
		rig r;
		r.bus.load(0x1000, {0xb8, 0x34, 0x12, 0xbb, 0x00, 0x01, 0xf7, 0xe3, 0xf4});   // mov ax,1234h; mov bx,100h; mul bx; hlt
		r.start(0x0100); r.go();
		check(r.cpu.dx() == 0x0012 && r.cpu.ax() == 0x3400 && (r.cpu.flags() & 0x801) == 0x801, "mul bx: 1234h*100h = 0012:3400, CF OF set");
		r.bus.load(0x1000, {0xf7, 0xf3, 0xf4});                                        // div bx; hlt
		r.start(0x0100); r.go();
		check(r.cpu.ax() == 0x1234 && r.cpu.dx() == 0, "div bx: 0012:3400 / 100h = 1234h r 0");
		r.bus.load(0x1000, {0xb8, 0x64, 0x00, 0xb1, 0x07, 0xf6, 0xf1, 0xf4});         // mov ax,100; mov cl,7; div cl; hlt
		r.start(0x0100); r.go();
		check(r.cpu.ax() == 0x020e, "div cl: 100/7 = 14 r 2");
		r.bus.load(0x1000, {0xb8, 0xf6, 0xff, 0xb1, 0x03, 0xf6, 0xe9, 0xf4});         // mov ax,-10; mov cl,3; imul cl; hlt
		r.start(0x0100); r.go();
		check(r.cpu.ax() == 0xffe2, "imul cl: (-10 & 0ffh) is 0f6h = -10, * 3 = -30");
		r.bus.load(0x00000, {0x00, 0x00, 0x00, 0x08});                                 // int 0 -> 0800:0000
		r.bus.load(0x8000, {0xba, 0xef, 0xbe, 0xf4});                                  // mov dx,0beefh; hlt
		r.bus.load(0x1000, {0xb8, 0x01, 0x00, 0xb1, 0x00, 0xf6, 0xf1, 0xf4});         // mov ax,1; mov cl,0; div cl
		r.start(0x0100); r.go();
		check(r.cpu.dx() == 0xbeef && r.cpu.cs() == 0x0800, "div by zero takes INT 0");
	}
	{   // NMI: vector read from 0x00008 in SP_CODE, pushes to SP_STACK
		rig r;
		r.bus.load(0x00008, {0x00, 0x00, 0x00, 0x06});                                 // NMI -> 0600:0000
		r.bus.load(0x6000, {0xb8, 0xcd, 0xab, 0xf4});                                  // mov ax,0abcdh; hlt
		r.bus.load(0x1000, {0xeb, 0xfe});                                              // jmp $
		r.start(0x0100);
		r.cpu.run(100);
		check(!r.cpu.halted() && r.cpu.ip() == 0, "spinning on jmp $");
		r.bus.log.clear();
		r.cpu.set_input_line(INPUT_LINE_NMI, ASSERT_LINE);
		r.go();
		check(r.cpu.ax() == 0xabcd && r.cpu.cs() == 0x0600, "NMI taken through vector 2");
		bool vec = true; for (u32 a = 8; a < 12; a++) vec &= r.bus.has('r', C, a);
		check(vec && !r.bus.has('r', P, 8), "NMI vector read at 00008-0000B is SP_CODE");
		check(r.bus.has('w', S, 0x200fe) && r.bus.has('w', S, 0x200fc) && r.bus.has('w', S, 0x200fa), "NMI pushes flags, CS, IP to SP_STACK");
		check(r.bus.w(0x200fc) == 0x0100 && r.bus.w(0x200fa) == 0x0000 && r.cpu.sp() == 0xfa, "NMI stacked CS:IP = 0100:0000");
		r.bus.log.clear();
		r.cpu.set_input_line(INPUT_LINE_NMI, ASSERT_LINE);   // still high: no new edge
		r.cpu.run(100);
		check(r.cpu.halted() && r.bus.log.empty(), "NMI is edge triggered");
	}
	{   // INT n and IRET
		rig r;
		r.bus.load(0x84, {0x10, 0x00, 0x00, 0x07});                                   // int 21h -> 0700:0010
		r.bus.load(0x7010, {0xb9, 0x21, 0x43, 0xcf});                                 // mov cx,4321h; iret
		r.bus.load(0x1000, {0xfb, 0xcd, 0x21, 0xf4});                                 // sti; int 21h; hlt
		r.start(0x0100); r.go();
		check(r.cpu.cx() == 0x4321 && r.cpu.cs() == 0x0100 && r.cpu.ip() == 4 && r.cpu.sp() == 0x100, "int 21h / iret");
		check((r.cpu.flags() & 0x200) && r.bus.w(0x200fe) & 0x200, "int pushed IF set, iret restored it");
	}
	{   // INTR: the vector comes from inta()
		rig r;
		r.bus.vector = 0x40;
		r.bus.load(0x100, {0x00, 0x00, 0x00, 0x09});                                  // vector 40h -> 0900:0000
		r.bus.load(0x9000, {0xba, 0x11, 0x11, 0xf4});                                 // mov dx,1111h; hlt
		r.bus.load(0x1000, {0xfa, 0xeb, 0xfe});                                       // cli; jmp $
		r.start(0x0100);
		r.cpu.set_input_line(INPUT_LINE_INT0, ASSERT_LINE);
		r.cpu.run(200);
		check(r.bus.intas == 0 && !r.cpu.halted(), "INTR masked while IF = 0");
		r.cpu.set_flags(r.cpu.flags() | 0x200);
		r.go();
		check(r.bus.intas == 1 && r.cpu.dx() == 0x1111, "INTR with IF = 1: inta() gives vector 40h");
		r.cpu.set_input_line(INPUT_LINE_INT0, CLEAR_LINE);
	}
	{   // HLT is left by an interrupt
		rig r;
		r.bus.load(0x100, {0x00, 0x00, 0x00, 0x09});
		r.bus.load(0x9000, {0xba, 0x22, 0x22, 0xf4});
		r.bus.load(0x1000, {0xfb, 0xf4});                                             // sti; hlt
		r.start(0x0100); r.go();
		check(r.cpu.halted() && r.cpu.run(50) == 50, "halted: run() consumes the whole budget");
		r.cpu.set_input_line(INPUT_LINE_INT0, ASSERT_LINE); r.cpu.run(1);
		r.cpu.set_input_line(INPUT_LINE_INT0, CLEAR_LINE); r.go();
		check(r.cpu.dx() == 0x2222, "INTR wakes the CPU from HLT");
	}
	{   // LOCK pin around the whole locked instruction ([tek8550]: MAME drives it for "lock in al,imm8" only)
		rig r;
		r.bus.ports[0x42] = 0x99;
		r.bus.load(0x1000, {0xf0, 0xe4, 0x42, 0xe4, 0x42, 0xf4});                     // lock in al,42h; in al,42h; hlt
		r.start(0x0100); r.go();
		check(r.bus.locks.size() == 2 && r.bus.locks[0] == 1 && r.bus.locks[1] == 0 && (r.cpu.ax() & 0xff) == 0x99 && !r.cpu.locked(), "lock in al,imm8: lock(1), read, lock(0), once");
	}
	{   // the pin brackets exactly the locked instruction's bus cycles; prefixes keep it pending
		rig r;
		r.bus.load(0x1000, {0xb0, 0x5a,                  // mov al,5ah
		                    0xf0, 0x26, 0x88, 0x07,      // lock es: mov [bx],al
		                    0x88, 0x07,                  // mov [bx],al        (not locked)
		                    0xf0, 0xec,                  // lock in al,dx
		                    0xf0, 0xf0, 0x26, 0x89, 0x07,// lock lock es: mov [bx],ax
		                    0xf4});
		r.start(0x0100); r.cpu.set_ds(0x0800); r.cpu.set_es(0x0900); r.cpu.set_bx(0x10); r.cpu.set_dx(0x77);
		bool during = false;
		r.cpu.trace_hook = [&](u32) { during |= r.cpu.locked(); };   // never locked at an instruction boundary
		r.go();
		std::vector<acc> d; for (auto &x : r.bus.log) if (!(x.kind == 'r' && x.space == O)) d.push_back(x);
		auto is = [&](size_t i, char k, int sp, u32 a) { return i < d.size() && d[i].kind == k && d[i].space == sp && d[i].addr == a; };
		auto lk = [&](size_t i, int st) { return i < d.size() && d[i].kind == 'L' && d[i].data == st; };
		check(lk(0, 1) && is(1, 'w', E, 0x9010) && lk(2, 0), "lock es: mov [bx],al: lock(1), ES write, lock(0)");
		check(is(3, 'w', P, 0x8010), "the next mov is not locked");
		check(lk(4, 1) && is(5, 'i', -1, 0x77) && lk(6, 0), "lock in al,dx: lock(1), port read, lock(0)");
		check(lk(7, 1) && is(8, 'w', E, 0x9010) && is(9, 'w', E, 0x9011) && lk(10, 0) && d.size() == 11, "lock lock es: mov [bx],ax: one lock(1)/lock(0) around both writes");
		check(!during && !r.cpu.locked(), "LOCK is released at every instruction boundary");
	}
	{   // lock iret: the pin covers IRET's three stack reads
		rig r;
		r.bus.load(0x200fa, {0x05, 0x00, 0x00, 0x01, 0x02, 0xf0});              // IP=0005 CS=0100 flags=F002
		r.bus.load(0x1000, {0xf0, 0xcf, 0x90, 0x90, 0x90, 0xf4});               // lock iret; ...; hlt at 0005
		r.start(0x0100); r.cpu.set_sp(0xfa); r.go();
		std::vector<acc> d; for (auto &x : r.bus.log) if (!(x.kind == 'r' && x.space == O)) d.push_back(x);
		bool ok = d.size() == 8 && d[0].kind == 'L' && d[0].data == 1 && d[7].kind == 'L' && d[7].data == 0;
		for (size_t i = 1; ok && i < 7; i++) ok = d[i].kind == 'r' && d[i].space == S && d[i].addr == 0x200fa + (i - 1);
		check(ok && r.cpu.ip() == 6 && r.cpu.sp() == 0x100, "lock iret: lock(1), six SP_STACK byte reads, lock(0)");
	}
	{   // IN / OUT
		rig r;
		r.bus.ports[0x3f8] = 0x77; r.bus.ports[0x30] = 0xcd; r.bus.ports[0x31] = 0xab;
		r.bus.load(0x1000, {0xb0, 0x5a, 0xe6, 0x10,        // mov al,5ah; out 10h,al
		                    0xe6, 0x11,                    // out 11h,al (odd port)
		                    0xba, 0xf8, 0x03, 0xec,        // mov dx,3f8h; in al,dx
		                    0x88, 0xc3,                    // mov bl,al
		                    0xb0, 0xa5, 0xee,              // mov al,0a5h; out dx,al
		                    0xb8, 0x34, 0x12, 0xe7, 0x20,  // mov ax,1234h; out 20h,ax
		                    0xe5, 0x30, 0xf4});            // in ax,30h; hlt
		r.start(0x0100); r.go();
		std::vector<acc> io; for (auto &x : r.bus.log) if (x.kind == 'i' || x.kind == 'o') io.push_back(x);
		bool ok = io.size() == 8
			&& io[0].kind == 'o' && io[0].addr == 0x10 && io[0].data == 0x5a
			&& io[1].kind == 'o' && io[1].addr == 0x11 && io[1].data == 0x5a
			&& io[2].kind == 'i' && io[2].addr == 0x3f8
			&& io[3].kind == 'o' && io[3].addr == 0x3f8 && io[3].data == 0xa5
			&& io[4].kind == 'o' && io[4].addr == 0x20 && io[4].data == 0x34
			&& io[5].kind == 'o' && io[5].addr == 0x21 && io[5].data == 0x12
			&& io[6].kind == 'i' && io[6].addr == 0x30 && io[7].kind == 'i' && io[7].addr == 0x31;
		check(ok, "out imm8 / out 11h (one byte to the odd port) / in dx / out dx / out ax / in ax");
		check((r.cpu.bx() & 0xff) == 0x77 && r.cpu.ax() == 0xabcd, "in al,dx and in ax,imm8 values");
	}
	{   // bus spaces chosen by segment
		rig r;
		r.bus.ram[0x8010] = 0x11; r.bus.ram[0x9010] = 0x22; r.bus.ram[0xa020] = 0x33; r.bus.ram[0x1010] = 0x44; r.bus.ram[0x8020] = 0x55; r.bus.ram[0xa010] = 0x66;
		r.bus.load(0x1000, {0x8a, 0x07,                  // mov al,[bx]         DS
		                    0x26, 0x8a, 0x07,            // mov al,es:[bx]      ES
		                    0x8a, 0x46, 0x00,            // mov al,[bp+0]       SS
		                    0x2e, 0x8a, 0x07,            // mov al,cs:[bx]      CS
		                    0x3e, 0x8a, 0x46, 0x00,      // mov al,ds:[bp+0]    DS
		                    0x36, 0x8a, 0x07,            // mov al,ss:[bx]      SS
		                    0xaa,                        // stosb               ES:DI
		                    0x50,                        // push ax             SS:SP
		                    0xf4});
		r.start(0x0100); r.cpu.set_ds(0x0800); r.cpu.set_es(0x0900); r.cpu.set_ss(0x0a00); r.cpu.set_bx(0x10); r.cpu.set_bp(0x20); r.cpu.set_di(0);
		r.go();
		std::vector<acc> d; bool fetch_ok = true;
		for (auto &x : r.bus.log) {
			if (x.kind == 'r' && x.space == O) { fetch_ok &= x.addr >= 0x1000 && x.addr < 0x1020; continue; }
			d.push_back(x);
		}
		auto is = [&](size_t i, char k, int sp, u32 a) { return i < d.size() && d[i].kind == k && d[i].space == sp && d[i].addr == a; };
		check(fetch_ok, "every opcode fetch is SP_OPCODES");
		check(is(0, 'r', P, 0x8010), "mov al,[bx]     -> SP_PROGRAM");
		check(is(1, 'r', E, 0x9010), "mov al,es:[bx]  -> SP_EXTRA");
		check(is(2, 'r', S, 0xa020), "mov al,[bp]     -> SP_STACK");
		check(is(3, 'r', C, 0x1010), "mov al,cs:[bx]  -> SP_CODE");
		check(is(4, 'r', P, 0x8020), "mov al,ds:[bp]  -> SP_PROGRAM");
		check(is(5, 'r', S, 0xa010), "mov al,ss:[bx]  -> SP_STACK");
		check(is(6, 'w', E, 0x9000) && d[6].data == 0x66, "stosb           -> SP_EXTRA");
		check(is(7, 'w', S, 0xa0fe) && is(8, 'w', S, 0xa0ff) && d.size() == 9, "push ax         -> SP_STACK");
	}
	{   // 8088 variant and the trace hook
		flat_bus bus; i8088_cpu_device cpu(bus);
		bus.load(0x1000, {0x90, 0x90, 0xf4});
		std::vector<u32> pcs; cpu.trace_hook = [&](u32 pc) { pcs.push_back(pc); };
		cpu.reset(); cpu.set_cs(0x0100); cpu.set_ip(0); cpu.run(100);
		check(cpu.is_8088() && cpu.halted() && pcs.size() == 3 && pcs[0] == 0x1000 && pcs[2] == 0x1002, "8088: runs, trace_hook sees each instruction");
	}
	{   // disassembler (16-bit mode)
		check(dasm({0xb8, 0x34, 0x12}) == "mov     ax,1234h", "dasm: mov ax,1234h");
		check(dasm({0x26, 0x8a, 0x07}) == "mov     al,es:[bx]", "dasm: mov al,es:[bx]");
		check(dasm({0x9a, 0x00, 0x00, 0x00, 0x03}) == "call    0300h:0h", "dasm: call far");
		check(dasm({0xf3, 0xa4}) == "rep     movsb   ", "dasm: rep movsb");
		rig r; auto d = r.cpu.create_disassembler();
		data_buffer buf; std::vector<u8> b = {0xcd, 0x21}; buf.rd = [&](offs_t a) -> u8 { return a < b.size() ? b[a] : 0; };
		std::ostringstream s; u32 len = d->disassemble(s, 0, buf, buf) & util::disasm_interface::LENGTHMASK;
		check(len == 2 && s.str() == "int     21h", "dasm via the core's create_disassembler: int 21h");
	}
	printf("%s: i86 core, %d failure(s)\n", fails ? "FAIL" : "ok  ", fails);
	return fails != 0;
}
