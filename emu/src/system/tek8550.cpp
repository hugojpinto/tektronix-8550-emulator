// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
#include "system/tek8550.h"
#include "system/tta_console.h"
#include "cpu/s2650/2650dasm.h"
#include "cpu/t11/t11dasm.h"
#include <sstream>
#include <cstdlib>

namespace {
struct cfg_t : s2650_disassembler::config { bool get_z80_mnemonics_mode() const override { return false; } } g_cfg;

std::string dasm2650(tek8301 &m, u16 pc)
{
	s2650_disassembler d(&g_cfg); data_buffer b; b.rd = [&](offs_t a) -> u8 { return m.read(a & 0x7fff); };
	std::ostringstream s; d.disassemble(s, pc, b, b); return s.str();
}
std::string dasm11(tek8501 &m, u16 pc)
{
	t11_disassembler d; data_buffer b;
	b.rd = [&](offs_t a) -> u8 { u16 w = m.peek16(a & 0xfffe); return (a & 1) ? u8(w >> 8) : u8(w); };
	std::ostringstream s; d.disassemble(s, pc, b, b); return s.str();
}
}

tek8550_system::~tek8550_system()
{
	for (FILE *f : { logf, hsif, auxf, trf, trf11, outf }) if (f) fclose(f);
}

void tek8550_system::show(FILE *f)
{
	tek8301 &a = *mdu; tek8501 &b = *dmu;
	fprintf(f, "8301: PC=%04x R0-3=%02x %02x %02x %02x PSU=%02x PSL=%02x LED=%02x  %s\r\n", a.cpu.pc(), a.cpu.reg(0), a.cpu.reg(1), a.cpu.reg(2), a.cpu.reg(3),
		a.cpu.psu(), a.cpu.psl(), a.leds(), dasm2650(a, a.cpu.pc()).c_str());
	fprintf(f, "8501: PC=%06o R0-5=%06o %06o %06o %06o %06o %06o SP=%06o PSW=%03o LEDs=%02o %s  %s\r\n", b.cpu.reg(7),
		b.cpu.reg(0), b.cpu.reg(1), b.cpu.reg(2), b.cpu.reg(3), b.cpu.reg(4), b.cpu.reg(5), b.cpu.reg(6), b.cpu.psw(), b.leds(),
		b.cpu.halted() ? "HALTED" : "", dasm11(b, b.cpu.reg(7)).c_str());
	fprintf(f, "%s\r\n", a.lp_state().c_str());
}

void tek8550_system::hlog(int dir, u8 c)
{
	if (!hsif) return;
	if (dir != last_dir) { fprintf(hsif, "\n%s", dir == 1 ? "8301>" : "8501>"); last_dir = dir; }
	fprintf(hsif, " %02x", c); fflush(hsif);
}

bool tek8550_system::start(bool feed_terminal, std::string &err)
{
	m_feed_ct = feed_terminal;
	mdu = std::make_unique<tek8301>();
	dmu = std::make_unique<tek8501>();
	dmu->fdc_lle = !opt.fdc_hle;
	if (!mdu->load_roms(opt.roms + "/8301", err) || !dmu->load_roms(opt.roms + "/8501", err)) return false;
	for (int d = 0; d < 4; d++) {
		if (opt.disks[d].empty()) continue;
		// -rw writes back every mounted image; originals under sources/ stay read-only
		bool wb = opt.writeback && opt.disks[d].find("sources/") == std::string::npos;
		if (opt.writeback && !wb) fprintf(stderr, "[drive %d: %s is an original; mounted without write-back]\n", d, opt.disks[d].c_str());
		if (!dmu->mount(d, opt.disks[d], err, wb)) return false;
		m_disc[d] = opt.disks[d];
	}

	logf = opt.logfile.empty() ? nullptr : fopen(opt.logfile.c_str(), "wb");
	hsif = opt.hsilog.empty() ? nullptr : fopen(opt.hsilog.c_str(), "w");
	auxf = opt.auxlog.empty() ? nullptr : fopen(opt.auxlog.c_str(), "wb");
	outf = opt.outputs_log.empty() ? nullptr : fopen(opt.outputs_log.c_str(), "w");
	if (outf) out.subscribe([this](const std::string &n, int v) { fprintf(outf, "%.6f %s %d\n", now(), n.c_str(), v); fflush(outf); });

	ct.send = [this](u8 b) { key_in(b); };
	ct.send_break = [this]() { mdu->term.host_break(); };
	mdu->term.on_tx = [this](u8 c) {
		c &= 0x7f;
		// DOS/50 echoing keys that were already shown while a line might have been a TTA command
		if (!m_echo_skip.empty()) { if (c == m_echo_skip.front()) { m_echo_skip.pop_front(); return; } m_echo_skip.clear(); }
		if (m_feed_ct || !opt.screenfile.empty() || !opt.snaps.empty()) { ct.feed(c); terminal_dirty = true; }
		if (on_term_tx) on_term_tx(c);
		if (logf) { fputc(c, logf); fflush(logf); }
	};
	dmu->aux.on_tx = [this](u8 c) {           // 8501 AUXILIARY (177560): the LSI-11's console
		if (auxf) { fputc(c & 0x7f, auxf); fflush(auxf); }
		aux8501.send(c);
	};

	// the HSI cable
	mdu->hsi_connected = true;
	mdu->hsi_tx = [this](u8 c) { hlog(1, c); if (m_power8501) dmu->hsi_rx(c); };
	dmu->hsi_tx = [this](u8 c) { hlog(2, c); if (m_power8301) mdu->hsi_rx(c); };
	mdu->hsi_cts = [this]() { return m_power8501 && !dmu->hsi_ncts(); };
	dmu->hsi_peer_ready = [this]() { return m_power8301 && !mdu->hsi_rx_busy(); };

	if (!opt.targetspec.empty()) {
		if (!proto.open(opt.targetspec, err)) return false;
		mdu->target = &proto;
	}
	if (!opt.remotespec.empty()) {                // 8301 REMOTE port (J102): COMM / HOST
		if (!remote.open(opt.remotespec, err)) return false;
		mdu->remote.on_tx = [this](u8 c) { remote.send(c); };
		// REMOTE BAUD is a rear-panel switch on the 8301 (the 8540 manual's setup for
		// 8550 transfers: 2400). 10 bits per character.
		if (opt.remote_baud < 110 || opt.remote_baud > 19200) { err = "-remote-baud must be 110..19200"; return false; }
		mdu->remote.char_cycles = int(tek8301::CPU_HZ * 10 / opt.remote_baud);
	}
	// the other RS-232 ports: open unless something is plugged in
	if (!opt.aux8301spec.empty()) {              // 8301 AUX (ACIA CC/CD)
		if (!aux8301.open(opt.aux8301spec, err, "8301 aux port")) return false;
		mdu->aux.on_tx = [this](u8 c) { aux8301.send(c); };
	}
	if (!opt.aux8501spec.empty() && !aux8501.open(opt.aux8501spec, err, "8501 aux port")) return false;
	if (!opt.printerspec.empty()) {              // 8501 PRINTER (177510)
		if (!printer.open(opt.printerspec, err, "8501 printer port")) return false;
		dmu->printer.on_tx = [this](u8 c) { printer.send(c); };
	}
	// modem lines of what is plugged into the 8501 ports: a printer, a terminal or a
	// host asserts DTR and RTS once connected; a wrap-back connector returns the port's own
	// CLEAR TO SEND
	auto lines = [](serial_link &l, serial_port &p) {
		p.peer_lines = [&l, &p]() {
			if (!l.connected()) return 0;
			if (l.loopback()) return p.cts_out() ? 3 : 0;   // RCSR bit 0 set = CTS asserted (the diagnostic's wrap-back test)
			return 3;
		};
	};
	lines(aux8501, dmu->aux);
	lines(printer, dmu->printer);
	mdu->mode_switch = u8(opt.sw);
	mdu->pm_ext_comparator = opt.pm_ext;
	if (opt.no_ep) mdu->m_epz80.present = false;
	mdu->board8085().present = opt.ep85;
	if (opt.ep86) {
		// the 8086/8088 emulator's kernel: two 1 KB PROMs, 160-1312-00 the even bytes and
		// 160-1311-00 the odd ones (the same board set serves both processors; the probe differs)
		// the 80186/80188 emulator: 160-2525-01 even, 160-2526-01 odd. Those are 2 KB parts of
		// a 4 KB kernel dumped as 1 KB each: the upper half is lost (high level in ep8086.cpp)
		std::vector<u8> ev, od;
		const bool k186 = opt.ep186;
		const std::string d = opt.roms + (k186 ? "/../emulator_processor/80186/" : "/../emulator_processor/8088/");
		auto rd = [](const std::string &f, std::vector<u8> &v) {
			FILE *fp = fopen(f.c_str(), "rb"); if (!fp) return false;
			v.resize(1024); size_t n = fread(v.data(), 1, v.size(), fp); fclose(fp); return n == 1024;
		};
		const char *fe = k186 ? "160-2525-01_emul.bin" : "160-1312-00_emul.bin", *fo = k186 ? "160-2526-01_emul.bin" : "160-1311-00_emul.bin";
		if (!rd(d + fe, ev) || !rd(d + fo, od)) { err = std::string("missing emulator kernel PROMs (") + fe + ", " + fo + ") in " + d; return false; }
		ep8086_board &b = mdu->m_ep86;
		b.prom.assign(k186 ? 4096 : 2048, 0xff);
		for (int i = 0; i < 1024; i++) { b.prom[2 * i] = ev[i]; b.prom[2 * i + 1] = od[i]; }
		b.prom_dumped = 2048;
		b.present = true; b.is8088 = opt.ep88; b.is186 = k186;
		b.wait_states = k186 ? 1 : 0;
		b.trace = getenv("TEK_EP86TRACE") != nullptr;
		b.restart();
	}
	mdu->prom.present = opt.prom_on;
	mdu->rtpa_board.present = opt.rtpa_on;
	mdu->rtpa_board.probe_loopback = opt.rtpa_probe;
	mdu->tta_board.present = opt.tta_on;
	if (opt.tta_on) m_tta_con = std::make_unique<tta_console>(*this);   // the TTA software: run mode from the start
	if (opt.tta_probe) mdu->tta_board.probe = [m = mdu.get()]() { return m->bank_lines(); };   // inferred TTA.TST option 7 fixture
	mdu->prom_dos50_fix = opt.prom_fix;
	if (!opt.prom_dev.empty()) { std::string e; mdu->prom.load_socket(opt.prom_dev, e); }
	else mdu->prom.socket.assign(0x10000, 0xff);
	if (!opt.ep_prom_file.empty()) {
		FILE *f = fopen(opt.ep_prom_file.c_str(), "rb");
		if (!f) { err = "cannot read " + opt.ep_prom_file; return false; }
		mdu->m_epz80.prom.resize(512); size_t n = fread(mdu->m_epz80.prom.data(), 1, 512, f); fclose(f);
		if (n != 512) { err = opt.ep_prom_file + ": expected 512 bytes, got " + std::to_string(n); return false; }
	}
	mdu->trace_io = getenv("TEK_IOTRACE8301") != nullptr;
	mdu->ep_trace = getenv("TEK_EPTRACE") != nullptr;
	trf = opt.tracefile.empty() ? nullptr : fopen(opt.tracefile.c_str(), "w");
	if (trf) {
		mdu->cpu.trace_hook = [this](u16 pc) {
			if (mdu->cycles() < opt.trace_at * tek8301::CPU_HZ) return;
			auto &c = mdu->cpu;
			fprintf(trf, "%04x: %-20s r0-3=%02x %02x %02x %02x psu=%02x t=%.4f\n", pc, dasm2650(*mdu, pc).c_str(), c.reg(0), c.reg(1), c.reg(2), c.reg(3), c.psu(), mdu->cycles() / tek8301::CPU_HZ);
		};
		mdu->lp_trace_hook = [this](u16 pc) {
			if (mdu->cycles() < opt.trace_at * tek8301::CPU_HZ) return;
			fprintf(trf, "  Z80 %04x\n", pc);
		};
	}
	trf11 = opt.trace11file.empty() ? nullptr : fopen(opt.trace11file.c_str(), "w");
	if (trf11) dmu->cpu.trace_hook = [this](u16 pc) {
		double t = dmu->cycles() / tek8501::CPU_HZ;
		if (t >= opt.trace11_at && t < opt.trace11_at + opt.trace11_len) fprintf(trf11, "%06o: %s\n", pc, dasm11(*dmu, pc).c_str());
	};
	mdu->power_on();
	dmu->power_on();
	out.set("8501:acon", 1);                      // AC ON lamp (rear mains switch)
	out.set("8501:on", 1);                       // DC on
	for (const char *n : { "8301:power", "8501:dc" }) out.set(n, 1);
	for (const char *n : { "8501:halt", "8301:restart", "8501:restart" }) out.set(n, 0);
	update_outputs();
	return true;
}

void tek8550_system::scripted_events()
{
	for (auto &t : opt.typed)
		if (!t.text.empty() && clock() >= t.at * tek8301::CPU_HZ) {
			for (size_t i = 0; i < t.text.size(); i++) {
				char ch = t.text[i];
				if (ch == '\\' && i + 1 < t.text.size()) {
					char e = t.text[++i];
					if (e == 'r') ch = '\r'; else if (e == 'n') ch = '\n'; else if (e == 'e') ch = 27;
					else if (e == 'x' && i + 2 < t.text.size()) { ch = char(strtol(t.text.substr(i + 1, 2).c_str(), nullptr, 16)); i += 2; }
					else ch = e;
				}
				key_in(u8(ch));
			}
			t.text.clear();
		}
	for (auto &pk : opt.pokes)
		if (!pk.text.empty() && clock() >= pk.at * tek8301::CPU_HZ) {
			size_t eq = pk.text.find('=');
			unsigned addr = unsigned(strtoul(pk.text.substr(0, eq).c_str(), nullptr, 16));
			for (size_t i = eq + 1; i + 1 < pk.text.size() + 1 && i + 2 <= pk.text.size(); i += 2)
				mdu->poke(addr++, u8(strtoul(pk.text.substr(i, 2).c_str(), nullptr, 16)));
			pk.text.clear();
		}
	for (auto &sw : opt.switches)
		if (!sw.text.empty() && clock() >= sw.at * tek8301::CPU_HZ) {
			size_t eq = sw.text.find('=');
			bool ok = eq == std::string::npos ? press(sw.text) : input(sw.text.substr(0, eq), atoi(sw.text.c_str() + eq + 1));
			if (!ok) fprintf(stderr, "[unknown switch: %s]\r\n", sw.text.c_str());
			sw.text.clear();
		}
	for (auto &dc : opt.discs)
		if (!dc.text.empty() && clock() >= dc.at * tek8301::CPU_HZ) {
			size_t eq = dc.text.find('=');
			int drive = atoi(dc.text.c_str()) & 3;
			std::string e;
			bool ok = eq == std::string::npos ? eject(drive, e) : mount(drive, dc.text.substr(eq + 1), e);
			if (!ok) fprintf(stderr, "[drive %d: %s]\r\n", drive, e.c_str());
			dc.text.clear();
		}
	for (auto &sn : opt.snaps)
		if (!sn.text.empty() && clock() >= sn.at * tek8301::CPU_HZ) {
			FILE *f = fopen(sn.text.c_str(), "w"); if (f) { fputs(ct.state().c_str(), f); fclose(f); }
			sn.text.clear();
		}
}

void tek8550_system::update_outputs()
{
	// 8301 port ED: D0-4 LEDs (1 = on), D5 front panel SELF TEST (0 = lit; notes/8301.md)
	// a unit switched off shows nothing
	int a = m_power8301 ? mdu->leds() : 0x20;
	if (a != m_out_8301) {
		m_out_8301 = a;
		for (int i = 0; i < 5; i++) out.set("8301:led" + std::to_string(i), (a >> i) & 1);
		out.set("8301:selftest", !((a >> 5) & 1));         // active low: lit from RESET until the tests pass
	}
	// 8301 system RAM board, port D2 D3-7 (1 = off)
	int r = m_power8301 ? mdu->ram_leds() : 0xf8;
	if (r != m_out_8301_ram) {
		m_out_8301_ram = r;
		for (int i = 0; i < 5; i++) out.set("8301:ramled" + std::to_string(i), !((r >> (i + 3)) & 1));
	}
	// 8501 utility board, LTC<15:11> (1 = off)
	int b = m_power8501 ? dmu->leds() : 0x1f;
	if (b != m_out_8501) {
		m_out_8501 = b;
		for (int i = 0; i < 5; i++) out.set("8501:led" + std::to_string(i), !((b >> i) & 1));
	}
	// front panels: 8301 SYSTEM (2650 running), PROGRAM (emulator processor running),
	// DMA (a transfer this slice) -- 070-2976 Fig. 1-3; 8501 PROCESSOR BUSY (LSI-11 running)
	bool dma = mdu->dma_transfers != m_dma_seen; m_dma_seen = mdu->dma_transfers;
	int p = m_power8301 ? (!mdu->cpu.halted() ? 1 : 0) | (mdu->lp_running() ? 2 : 0) | (dma ? 4 : 0) : 0;
	if (m_power8501 && !m_held8501 && !dmu->cpu.halted()) p |= 8;
	if (p != m_out_panel) {
		int ch = m_out_panel < 0 ? 15 : p ^ m_out_panel;
		m_out_panel = p;
		if (ch & 1) out.set("8301:system", p & 1);
		if (ch & 2) out.set("8301:program", (p >> 1) & 1);
		if (ch & 4) out.set("8301:dma", (p >> 2) & 1);
		if (ch & 8) out.set("8501:busy", (p >> 3) & 1);
	}
	// drive activity lamps: selected with the head loaded (low-level controller only)
	int m = 0;
	if (dmu->fdc_lle && m_power8501) for (int d = 0; d < 4; d++) if (dmu->fdc.drive_lamp(d)) m |= 1 << d;
	if (m != m_out_drives) {
		if (m_out_drives < 0) for (int d = 0; d < 4; d++) out.set("drive" + std::to_string(d) + ":disc", dmu->mounted(d));
		m_out_drives = m;
		for (int d = 0; d < 4; d++) out.set("drive" + std::to_string(d) + ":active", (m >> d) & 1);
	}
}

void tek8550_system::tick()
{
	scripted_events();
	for (size_t i = 0; i < m_releases.size();)                // momentary switches let go
		if (now() >= m_releases[i].at) { std::string n = m_releases[i].name; m_releases.erase(m_releases.begin() + long(i)); input(n, 0); }
		else i++;
	if (m_power8301 && !m_held8301) mdu->run_slice(int(tek8301::CPU_HZ * SLICE_S));
	else m_held_cycles += u64(tek8301::CPU_HZ * SLICE_S);   // switched off or held in reset
	if (m_power8501 && !m_held8501) dmu->run_slice(int(tek8501::CPU_HZ * SLICE_S));
	if (remote.active()) remote.poll([this](u8 c) { if (m_power8301) mdu->remote.host_rx(c); });
	if (aux8301.active()) aux8301.poll([this](u8 c) { if (m_power8301) mdu->aux.host_rx(c); });
	if (printer.active()) printer.poll([this](u8 c) { if (m_power8501) dmu->printer.host_rx(c); });
	// the 8501 console: while the LSI-11 is halted its microcode ODT owns the port
	if (aux8501.active()) aux8501.poll([this](u8 c) {
		if (!m_power8501) return;
		if (dmu->cpu.halted()) dmu->odt_char(c); else dmu->aux.host_rx(c);
	});
	bool console = aux8501.connected();
	if (dmu->cpu.halted() && m_power8501 && !m_held8501) {
		if (!console && !m_halt8501) dmu->cpu.set_halted(false);   // HALT with nobody at the console: carry on
		else if (!m_odt_seen) { if (console) dmu->odt_enter(); m_odt_seen = true; }
	} else m_odt_seen = false;
	m_ticks++;
	update_outputs();
	if (m_ticks % 1000 == 0) { std::string e; if (!dmu->flush(e)) fprintf(stderr, "[disk save failed: %s]\r\n", e.c_str()); }
}

bool tek8550_system::mount(int drive, const std::string &path, std::string &err)
{
	bool wb = opt.writeback && path.find("sources/") == std::string::npos;
	if (dmu->mounted(drive) && !dmu->eject(drive, err)) return false;
	if (!dmu->mount(drive, path, err, wb, true)) return false;
	m_disc[drive & 3] = path;
	out.set("drive" + std::to_string(drive) + ":disc", 1);
	return true;
}

bool tek8550_system::eject(int drive, std::string &err)
{
	bool ok = dmu->eject(drive, err);
	m_disc[drive & 3].clear();
	out.set("drive" + std::to_string(drive) + ":disc", 0);
	return ok;
}

void tek8550_system::power(int unit, bool on)
{
	if (unit == 8301) {
		if (on == m_power8301) return;
		m_power8301 = on;
		if (on) { m_held8301 = false; mdu->power_on(); }           // system RAM comes up cleared
		out.set("8301:power", on);
	} else {
		if (on == m_power8501) return;
		m_power8501 = on;
		if (on) { m_held8501 = false; dmu->power_on(); if (m_halt8501) dmu->cpu.set_halted(true); }
		out.set("8501:dc", on);
		out.set("8501:on", on);
	}
	update_outputs();
}

bool tek8550_system::input(const std::string &name, int value)
{
	if (name == "8301:power") power(8301, value != 0);
	else if (name == "8501:dc") power(8501, value != 0);
	else if (name == "8501:halt") {
		// HALT: the LSI-11 stops into ODT. RUN: with a console attached, ODT's P resumes it
		// (as on the real KD11); with nobody at the console, it resumes by itself.
		m_halt8501 = value != 0;
		dmu->halt_switch = m_halt8501;
		if (m_power8501 && (m_halt8501 || !aux8501.connected())) dmu->cpu.set_halted(m_halt8501);
		out.set("8501:halt", m_halt8501);
	}
	else if (name == "8301:restart") {
		// held: the unit is held in reset; released: the 2650 starts from its boot PROM
		if (value && m_power8301) mdu->restart();
		m_held8301 = value != 0 && m_power8301;
		out.set("8301:restart", value != 0);
	}
	else if (name == "8501:restart") {
		// held: the unit is held in reset; released: the LSI-11 starts from its boot PROM
		if (!value && m_held8501 && m_power8501) { dmu->restart(); if (m_halt8501) dmu->cpu.set_halted(true); }
		m_held8501 = value != 0 && m_power8501;
		out.set("8501:restart", value != 0);
	}
	else if (name == "restart") { input("8301:restart", value); input("8501:restart", value); }
	else if (name == "8301:modesw") mdu->mode_switch = u8(value);
	else return false;
	return true;
}

bool tek8550_system::press(const std::string &name, double seconds)
{
	if (name == "restart") return press("8301:restart", seconds) && press("8501:restart", seconds);
	if (name != "8301:restart" && name != "8501:restart") return false;
	input(name, 1);
	release_at(name, now() + seconds);
	return true;
}

void tek8550_system::release_at(const std::string &name, double at)
{
	if (name == "restart") { release_at("8301:restart", at); release_at("8501:restart", at); return; }
	for (auto &r : m_releases) if (r.name == name) { r.at = std::max(r.at, at); return; }
	m_releases.push_back({ at, name });
}

void tek8550_system::shutdown(bool show_state)
{
	if (!opt.screenfile.empty()) { FILE *f = fopen(opt.screenfile.c_str(), "w"); if (f) { fputs(ct.text().c_str(), f); fclose(f); } }
	if (!opt.targetspec.empty()) fprintf(stderr, "[prototype link: %lu requests, %lu WAIT states]\n", proto.requests(), proto.wait_total());
	{ std::string e; if (!dmu->flush(e, true)) fprintf(stderr, "disk save failed: %s\n", e.c_str()); }
	{ std::string e; if (!mdu->prom.save_socket(e)) fprintf(stderr, "PROM image save failed: %s\n", e.c_str()); }
	if (show_state) { fprintf(stderr, "\n"); show(stdout); }
	for (FILE **f : { &logf, &hsif, &auxf, &trf, &trf11, &outf }) if (*f) { fclose(*f); *f = nullptr; }
	if (!opt.dumpfile.empty()) {   // raw memory images: <file>.8501 (64 KB, LE words) and <file>.8301 (CPU view, 32 KB)
		FILE *f = fopen((opt.dumpfile + ".8501").c_str(), "wb");
		for (u32 a = 0; a < 0x10000; a += 2) { u16 w = dmu->peek16(a); fputc(w & 0xff, f); fputc(w >> 8, f); }
		fclose(f);
		f = fopen((opt.dumpfile + ".8301").c_str(), "wb");
		for (u32 a = 0; a < 0x8000; a++) fputc(mdu->read(a), f);
		fclose(f);
		f = fopen((opt.dumpfile + ".sys").c_str(), "wb");
		for (u32 a = 0; a < 0x10000; a++) fputc(mdu->sysmem(u16(a)), f);
		fclose(f);
		f = fopen((opt.dumpfile + ".prog").c_str(), "wb");
		for (u32 a = 0; a < 0x10000; a++) fputc(mdu->progmem(u16(a)), f);
		fclose(f);
	}
}

// ---- TTA command entry
void tek8550_system::tta_print(const std::string &s)
{
	if (m_feed_ct || !opt.screenfile.empty() || !opt.snaps.empty()) { for (char c : s) ct.feed(u8(c)); terminal_dirty = true; }
	if (!m_feed_ct) { fwrite(s.data(), 1, s.size(), stdout); fflush(stdout); }
}

std::string tek8550_system::tta_command(const std::string &line)
{
	if (!mdu->tta_board.present) { std::string e = "TTA not installed (start with -tta)\r\n"; tta_print(e); return e; }
	if (!m_tta_con) m_tta_con = std::make_unique<tta_console>(*this);
	std::string out = m_tta_con->run(line);
	tta_print(out);
	return out;
}

// The keyboard, as the later DOS/50 with the TTA commands would have read it. A line that
// may be a TTA command is held (and shown) here until its first word decides: a TTA command
// runs here, and DOS/50 then gets an empty line for a fresh prompt; anything else goes to
// DOS/50, whose echo of the held keys is then dropped.
void tek8550_system::key_in(u8 c)
{
	if (!m_tta_con) { mdu->term.host_rx(c); return; }
	auto release = [&]() {
		for (char k : m_kb_line) { mdu->term.host_rx(u8(k)); m_echo_skip.push_back(u8(k)); }
		m_kb_line.clear(); m_kb_hold = false; m_kb_start = false;
	};
	auto could_be = [](const std::string &l) {        // a prefix of a TTA command, or one and a space
		static const char *n[] = { "acq", "ad", "bre", "bus", "cons", "cou", "ctr", "data", "disp", "eve", "pro", "qua", "tclr", "ts" };
		size_t sp = l.find(' ');
		std::string w = l.substr(0, sp);
		for (char &ch : w) ch = char(tolower(u8(ch)));
		if (sp != std::string::npos) return tta_console::is_command(w);
		for (const char *k : n) if (std::string(k).compare(0, w.size(), w) == 0) return true;
		return false;
	};
	if (m_kb_hold) {
		if (c == '\r' || c == '\n') {
			std::string w = m_kb_line.substr(0, m_kb_line.find(' '));
			if (tta_console::is_command(w)) {
				tta_print("\r\n");
				std::string l = m_kb_line; m_kb_line.clear(); m_kb_hold = false; m_kb_start = true;
				tta_command(l);
				mdu->term.host_rx('\r');         // DOS/50: an empty line, a new prompt
				m_echo_skip.push_back('\r');
				return;
			}
			release(); mdu->term.host_rx(c); m_kb_start = true; return;
		}
		if (c == 0x08 || c == 0x7f) {
			if (!m_kb_line.empty()) { m_kb_line.pop_back(); tta_print("\b \b"); }
			if (m_kb_line.empty()) m_kb_hold = false;
			return;
		}
		if (c < 0x20) { release(); mdu->term.host_rx(c); return; }
		m_kb_line += char(c);
		tta_print(std::string(1, char(c)));
		if (!could_be(m_kb_line)) release();
		return;
	}
	if (m_kb_start && isalpha(c) && could_be(std::string(1, char(c)))) {
		m_kb_hold = true; m_kb_line = std::string(1, char(c));
		tta_print(m_kb_line);
		return;
	}
	m_kb_start = c == '\r' || c == '\n';
	mdu->term.host_rx(c);
}
