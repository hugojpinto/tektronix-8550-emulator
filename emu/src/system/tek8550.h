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
	bool finished() const { return opt.max_cycles >= 0 && (long long)mdu->cycles() >= opt.max_cycles; }
	double now() const { return mdu->cycles() / tek8301::CPU_HZ; }
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
	// "8301:restart", "8501:restart", "restart" (both), "8301:modesw" (value = DIP byte).
	// Returns false for an unknown name.
	bool input(const std::string &name, int value = 1);

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
	FILE *logf = nullptr, *hsif = nullptr, *auxf = nullptr, *trf = nullptr, *trf11 = nullptr, *outf = nullptr;
	int last_dir = 0;
	u64 m_ticks = 0;
	int m_out_8301 = -1, m_out_8301_ram = -1, m_out_8501 = -1, m_out_drives = -1, m_out_panel = -1;
	u64 m_dma_seen = 0;
	std::string m_disc[4];

	void scripted_events();
	void update_outputs();
	void hlog(int dir, u8 c);
};
#endif
