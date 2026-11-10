// Tektronix 8550F30 PROM Programmer Controller -- high-level emulation.
//
// The real controller is an 8085A microcomputer with its own firmware (not dumped),
// a five-channel programmable power supply and a characteristic-module socket
// (070-3903). The 8301 sees it through two I/O ports and two interrupts. This model
// reproduces that interface and the firmware's command conversation, reverse-engineered
// from the 8550 diagnostic PROM.TST V1.2 and DOS/50's PROM driver (notes/8301.md).
//
// Interface (8301 side)
//   D1 write : data latch to the controller      D1 read : data latch from the controller
//   D0 read  : status byte posted by the firmware
//   D0 write : bit 3 = 0  latch Q[bits 2-0] <- bit 4   (Q7 = command/data flag: 17 cmd, 07 data)
//              bit 3 = 1  pulse bits 2-0: 3 byte ready (0B)  4 reset (0C)
//                                         5 ack int 14 (0D)  6 ack int 13 (0E)
//   int 14 (vector 1C): status posted          int 13 (vector 1A): data-phase step
//   (level requests: PROM.TST and DOS/50 acknowledge them with the 0D / 0E pulses)
// Conversation: command -> status (phase in bits 6-5) -> host A0 -> phase bytes -> status ...
//   phase 60 = wants parameters, 20 = accepts data, 40 = sends data, 00 = done; bit 0 = error.
#ifndef TEK_PROM_PROGRAMMER_H
#define TEK_PROM_PROGRAMMER_H
#include "emu.h"
#include <deque>
#include <vector>
#include <string>
#include <functional>

class prom_programmer
{
public:
	bool present = true;
	static constexpr int RESPONSE_CYCLES = 400;    // 2650 cycles (200 us) for the 8085 to answer
	// the emulated characteristic module: Intel-style UV EPROMs (erased = FF, 8-bit words).
	// The real modules' contents are not known; this one reports part number 160-0000-00.
	struct device_type { const char *name; u32 size; };
	static const device_type DEVICES[];
	static const int MODULE_DEVICES;
	static std::vector<u8> module_info();
	bool int13() const { return present && m_int13; }   // request lines to the 8301 (level),
	bool int14() const { return present && m_int14; }   // cleared by the 0E/0D acknowledges or 0C

	void reset();
	u8   last_command() const { return m_cmd; }
	u8   read(int port);                           // 0 = D0, 1 = D1
	void write(int port, u8 d);
	void tick(int cycles2650);

	// a conversation step
	struct step {
		enum kind_t { STATUS, PARAMS, SEND, RECV, CALL } kind;
		u8 status = 0;                             // STATUS
		int count = 0;                             // PARAMS / RECV
		std::vector<u8> bytes;                     // SEND
		std::function<void(prom_programmer &)> fn; // CALL: computes further steps from params
	};
	void then(step s) { m_steps.push_back(std::move(s)); }
	void then_status(u8 s) { step x{step::STATUS}; x.status = s; then(x); }
	void then_params(int n) { step x{step::PARAMS}; x.count = n; then(x); }
	void then_send(std::vector<u8> b) { step x{step::SEND}; x.bytes = std::move(b); then(x); }
	void then_recv(int n) { step x{step::RECV}; x.count = n; then(x); }
	void then_call(std::function<void(prom_programmer &)> f) { step x{step::CALL}; x.fn = std::move(f); then(x); }
	std::vector<u8> params, received;              // collected by PARAMS / RECV

	// the device in the ZIF socket, backed by an image file (-prom-dev FILE)
	std::vector<u8> socket;                        // contents (erased EPROM = FF)
	std::string socket_file;
	size_t socket_size = 0;                        // bytes to save: the file's size, or the highest address programmed
	bool socket_dirty = false;
	bool load_socket(const std::string &path, std::string &err);
	bool save_socket(std::string &err);
	u8 socket_rd(u32 a) const { return a < socket.size() ? socket[a] : 0xff; }

private:
	// interface
	u8 m_in = 0, m_reply = 0, m_status = 0;
	bool m_cd = false;                             // Q7: next byte is a command
	bool m_int13 = false, m_int14 = false;
	struct event { int delay; int irq; u8 value; };
	std::deque<event> m_events;                    // answers in flight (the 8085's latency)
	// conversation
	enum { IDLE, AWAIT_PROCEED, AWAIT_FINAL_ACK, IN_PARAMS, IN_SEND, IN_RECV } m_state = IDLE;
	std::deque<step> m_steps;
	step m_cur{step::STATUS};
	size_t m_pos = 0;
	u8 m_cmd = 0;

	void byte_in(u8 b, bool cmd);
	// programmable power supply (070-3903 Table 1-2): five channels, channel 4 with two
	// ranges. Ideal hardware: the sense A/D reads exactly what the D/A was set to.
	struct psu_range { u8 id, psc; double vmin, vmax, sense_max; };
	static const psu_range PSU[6];
	struct psu_state { bool on = false; u8 dac = 0; } m_psu[6];
	static std::vector<u8> sense_record(int r, u8 dac);   // 10-byte report (PROM.TST layout)
	void command(u8 c);
	void advance();                                // run the next step
	void post_status(u8 s) { m_events.push_back({RESPONSE_CYCLES, 14, s}); }
	void post_data(u8 v) { m_events.push_back({RESPONSE_CYCLES, 13, v}); }
	void post_request() { m_events.push_back({RESPONSE_CYCLES, 13, m_reply}); }
	void log(const char *fmt, ...);
};
#endif
