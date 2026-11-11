// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// tek8550: the complete 8550 MDL as one system -- 8301 MDU + 8501 DMU joined by the
// HSI cable, the CT8500 on the system terminal port, the optional prototype and
// REMOTE links, and the scripted events (-type, -snap, -poke).
//
// The system has no notion of wall-clock time: a front end calls tick() (one
// 100 us lockstep slice of emulated time) as fast or as slowly as it likes. The
// tty front end (main8550.cpp) and the SDL front end both drive this class.
#ifndef TEK_SYSTEM_TEK8550_H
#define TEK_SYSTEM_TEK8550_H
#include "machine/tek8301.h"
#include "machine/tek8501.h"
#include "host/ct8500.h"
#include "host/ipc_target.h"
#include "host/serial_link.h"
#include "system/options.h"
#include "system/outputs.h"
#include <memory>
#include <string>
#include <cstdio>

class tek8550_system
{
public:
	static constexpr double SLICE_S = 0.0001;   // both machines run in 100 us lockstep

	explicit tek8550_system(const tek8550_options &o) : opt(o) {}
	~tek8550_system();

	// load ROMs, mount discs, open logs and links, power on.
	// feed_terminal: run the CT8500 model on the system port (its replies reach the 8301).
	bool start(bool feed_terminal, std::string &err);
	void tick();                                   // one 100 us slice
	bool finished() const { return opt.max_cycles >= 0 && (long long)clock() >= opt.max_cycles; }
	double now() const { return clock() / tek8301::CPU_HZ; }
	// the system clock in 2650 cycles: the 8301's own count, plus the time it spent
	// switched off or held in RESTART (so time keeps flowing for scripts and front ends)
	u64 clock() const { return mdu->cycles() + m_held_cycles; }
	u64 ticks() const { return m_ticks; }
	// end of run: screen file, link stats, disc and PROM write-back, memory dumps.
	// show_state prints both CPUs' registers (batch runs).
	void shutdown(bool show_state);

	// the system terminal port
	ct8500 &terminal() { return ct; }
	bool terminal_dirty = false;                   // set when the 8301 sent the CT8500 something
	std::function<void(u8)> on_term_tx;            // every byte the 8301 sends (7-bit)
	void term_rx(u8 c) { mdu->term.host_rx(c); }   // a byte to the 8301 (bypassing the CT8500)

	// named outputs ("8301:led0", "8501:led3", ...; 1 = lit) and inputs
	outputs out;
	// Front-panel switches, as the hardware has them:
	//   "8301:power", "8501:dc"   rockers: 1 on, 0 off (a unit switched on runs its power-up)
	//   "8501:halt"               HALT/RUN toggle: 1 HALT (the LSI-11 halts into ODT), 0 RUN
	//   "8301:restart", "8501:restart"  momentary: 1 held (the unit is held in reset), 0 released
	//   "8301:modesw"             the mode DIP switch (value = the switch byte)
	// Returns false for an unknown name. Switch positions are also outputs of the same name.
	bool input(const std::string &name, int value = 1);
	// a momentary switch pressed for a human-like time (default 0.2 s of machine time);
	// "restart" presses both units' RESTART
	bool press(const std::string &name, double seconds = 0.2);
	// release a held momentary switch, but no earlier than the given machine time
	void release_at(const std::string &name, double at);

	// discs: insert at run time (the drive reports a disc change) / write back and remove
	bool mount(int drive, const std::string &path, std::string &err);
	bool eject(int drive, std::string &err);
	// the image in a drive ("" = none), however it got there: command line, -mount, monitor, GUI
	const std::string &disc(int drive) const { return m_disc[drive & 3]; }

	void show(FILE *f);                            // registers of both CPUs (monitor)
	tek8301 &mdu_unit() { return *mdu; }
	tek8501 &dmu_unit() { return *dmu; }

private:
	tek8550_options opt;
	std::unique_ptr<tek8301> mdu;
	std::unique_ptr<tek8501> dmu;
	ct8500 ct;
	bool m_feed_ct = false;
	ipc_target proto;
	serial_link remote;
	serial_link aux8301, aux8501, printer;   // 8301 AUX, 8501 AUXILIARY (console), 8501 PRINTER
	FILE *logf = nullptr, *hsif = nullptr, *auxf = nullptr, *trf = nullptr, *trf11 = nullptr, *outf = nullptr;
	int last_dir = 0;
	u64 m_ticks = 0;
	int m_out_8301 = -1, m_out_8301_ram = -1, m_out_8501 = -1, m_out_drives = -1, m_out_panel = -1;
	u64 m_dma_seen = 0;
	std::string m_disc[4];
	bool m_power8301 = true, m_power8501 = true, m_halt8501 = false, m_held8301 = false, m_held8501 = false;
	u64 m_held_cycles = 0;
	bool m_odt_seen = false;   // the 8501 was halted (in ODT) at the end of the last slice
	struct release_t { double at; std::string name; };
	std::vector<release_t> m_releases;
	void power(int unit, bool on);

	void scripted_events();
	void update_outputs();
	void hlog(int dir, u8 c);
};
#endif
