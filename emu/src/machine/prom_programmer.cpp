// Tektronix 8550F30 PROM Programmer Controller -- high-level emulation (see header).
#include "machine/prom_programmer.h"
#include <cstdio>
#include <cstdarg>
#include <cstdlib>

static const bool g_trace = getenv("TEK_PROMTRACE") != nullptr;

void prom_programmer::log(const char *fmt, ...)
{
	if (!g_trace) return;
	va_list ap; va_start(ap, fmt);
	fprintf(stderr, "[prom ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "]\n");
	va_end(ap);
}

void prom_programmer::reset()
{
	m_in = m_reply = m_status = 0;
	m_cd = false;
	m_int13 = m_int14 = false;
	m_events.clear(); m_steps.clear();
	m_state = IDLE; m_pos = 0;
}

u8 prom_programmer::read(int port)
{
	if (!present) return 0x00;                     // unmapped ports read 00 ("board absent")
	u8 v = port == 0 ? m_status : m_reply;
	log("rd %s = %02x", port == 0 ? "D0" : "D1", v);
	return v;
}

void prom_programmer::write(int port, u8 d)
{
	if (!present) return;
	log("wr %s <- %02x", port == 0 ? "D0" : "D1", d);
	if (port == 1) { m_in = d; return; }
	if (!(d & 0x08)) {                             // addressable latch
		if ((d & 7) == 7) m_cd = d & 0x10;
		return;
	}
	switch (d & 7) {                               // pulses
	case 3: byte_in(m_in, m_cd); break;            // byte ready for the controller
	case 4: reset(); break;
	case 5: m_int14 = false; break;                // acknowledge the status interrupt
	case 6: m_int13 = false; break;                // acknowledge the data interrupt
	default: break;
	}
}

void prom_programmer::tick(int cycles)
{
	if (m_events.empty()) return;
	event &e = m_events.front();
	e.delay -= cycles;
	if (e.delay > 0) return;
	if (e.irq == 13) m_reply = e.value; else m_status = e.value;
	log("%s %02x", e.irq == 13 ? "int13 data" : "int14 status", e.value);
	if (e.irq == 13) m_int13 = true; else m_int14 = true;
	m_events.pop_front();
}

// run steps until one has to wait for the host
void prom_programmer::advance()
{
	for (;;) {
		if (m_steps.empty()) { m_state = IDLE; return; }
		m_cur = std::move(m_steps.front()); m_steps.pop_front(); m_pos = 0;
		switch (m_cur.kind) {
		case step::CALL: m_cur.fn(*this); continue;
		case step::STATUS:
			post_status(m_cur.status);
			// phase 00: the conversation ends. The host acknowledges the final status with
			// A0 too, answered by one int 13 without data (DOS/50: state 'done' -> idle;
			// PROM.TST waits for any interrupt). Otherwise wait for the host's A0 to proceed.
			if (!(m_cur.status & 0x60)) { m_steps.clear(); m_state = AWAIT_FINAL_ACK; }
			else m_state = AWAIT_PROCEED;
			return;
		case step::PARAMS: params.clear(); m_state = IN_PARAMS; post_request(); return;
		case step::RECV: received.clear(); m_state = IN_RECV; post_request(); return;
		case step::SEND:
			if (m_cur.bytes.empty()) continue;
			m_state = IN_SEND; post_data(m_cur.bytes[0]); m_pos = 1; return;
		}
	}
}

// the firmware's view: one byte arrived (command or data)
void prom_programmer::byte_in(u8 b, bool cmd)
{
	log("%s %02x (state %d)", cmd ? "command" : "data", b, m_state);
	if (cmd) {
		if (b == 0xa0 && m_state == AWAIT_PROCEED) { advance(); return; }   // proceed
		if (b == 0xa0 && m_state == AWAIT_FINAL_ACK) { m_state = IDLE; post_request(); return; }
		if (b == 0xa0 && m_state == IDLE) { log("A0 while idle"); return; }
		if (b == 0xb0) { m_steps.clear(); m_state = IDLE; post_status(0x00); return; }   // end / abort
		command(b);
		return;
	}
	switch (m_state) {
	case IN_PARAMS:
		params.push_back(b);
		if ((int)params.size() < m_cur.count) post_request(); else advance();
		break;
	case IN_RECV:
		received.push_back(b);
		if ((int)received.size() < m_cur.count) post_request(); else advance();
		break;
	case IN_SEND:                                  // the host took a byte: send the next one
		if (m_pos < m_cur.bytes.size()) post_data(m_cur.bytes[m_pos++]);
		else advance();
		break;
	default:
		log("unexpected data byte %02x", b);
		break;
	}
}

// Power channels: id (0-based; PROM.TST prints id + 1), internal channel (PSC), control range, sense full scale (|V|).
// Channel 4 has a low (-5..-25 V) and a high (-23..-40 V) range; 5 is fixed 5 V.
const prom_programmer::psu_range prom_programmer::PSU[6] = {
	{0, 0,   4.0,  30.0, 30.0}, {1, 1,   4.0,  30.0, 30.0}, {2, 2,   4.0, 12.0, 12.0},
	{3, 3,  -5.0, -25.0, 25.0}, {3, 4, -23.0, -40.0, 40.0}, {4, 5,   5.2,  5.2,  5.25},
};

// A sense report: PSCHID, DAI (D/A input), ADO (A/D output), VLD (expected volts: high
// nibble of byte 3 = sixteenths, byte 4 = integer), DADO (expected A/D), VLA (actual
// volts, same encoding), NSR (supply range), PSC (internal channel). Field order as
// PROM.TST stores it at 264F-2658; magnitudes for the negative channel.
std::vector<u8> prom_programmer::sense_record(int r, u8 dac)
{
	const psu_range &p = PSU[r];
	double v = p.vmin + (p.vmax - p.vmin) * dac / 255.0;
	double mag = v < 0 ? -v : v;
	int adc = int(mag / p.sense_max * 255.0 + 0.5); if (adc > 255) adc = 255;
	int vi = int(mag); int vf = int((mag - vi) * 16.0 + 0.5); if (vf > 15) { vf = 0; vi++; }
	u8 vlo = u8(vf << 4), vhi = u8(vi);
	return { p.id, dac, u8(adc), vlo, vhi, u8(adc), vlo, vhi, u8(r == 4 ? 1 : 0), p.psc };
}

const prom_programmer::device_type prom_programmer::DEVICES[] = { {"2708", 0x400}, {"2716", 0x800}, {"2732", 0x1000} };
const int prom_programmer::MODULE_DEVICES = 3;

// Module information (DOS/50 PTYPE overlay, PROM.TST option 13): a 10-byte header
// (bytes 3-5 = the module's part number 160-xxxx-yy in BCD, byte 9 = number of device
// types), then the device names each ended by 0D, FF, a 3-byte maximum address per
// device, '!' (21), and a flags byte per device (high nibble nonzero = erased state
// high, low bits 01/02/04 = word length 4/8/16).
std::vector<u8> prom_programmer::module_info()
{
	std::vector<u8> v = {0, 0, 0, 0x00, 0x00, 0x00, 0, 0, 0, u8(MODULE_DEVICES)};
	for (int i = 0; i < MODULE_DEVICES; i++) { for (const char *c = DEVICES[i].name; *c; c++) v.push_back(u8(*c)); v.push_back(0x0d); }
	v.push_back(0xff);
	for (int i = 0; i < MODULE_DEVICES; i++) { u32 m = DEVICES[i].size - 1; v.push_back(u8(m >> 16)); v.push_back(u8(m >> 8)); v.push_back(u8(m)); }
	v.push_back(0x21);
	for (int i = 0; i < MODULE_DEVICES; i++) v.push_back(0x12);   // erased high, 8-bit words
	return v;
}

// the command set (as far as reverse-engineered). Status: bit 0 = OK, bits 6-5 = next
// phase (60 parameters, 40 sends data, 20 accepts data, 00 done), bit 7 = unrecognized
// command, bits 1-2 = failure reasons when bit 0 is clear.
void prom_programmer::command(u8 c)
{
	m_cmd = c;
	m_steps.clear();
	switch (c) {
	case 0xaa:                                     // interface echo (PROM.TST system interface test)
		then_status(0x61);
		then_params(1);
		then_status(0x41);
		then_call([](prom_programmer &p) { p.then_send({p.m_cmd, p.params[0]}); p.then_status(0x00); });
		break;
	// the controller's own self-tests: 08 characteristic module checksum, 18 module test,
	// 28 front panel, 30 RAM, 40-42 ROM (all / 1 / 2),
	// 50 8085 processor, 60 all power-up tests. +4 = firmware looping (PROM.TST asks).
	// Status 00 = test complete and passed; 01 = executing (looping until aborted).
	// The emulated controller is healthy, so they pass -- this is the controller judging
	// itself, not a check of dumped bytes (cf. the Z80 emulator PROM, notes/8301.md).
	case 0x18: case 0x1c:                          // characteristic module test: runs until aborted
		then_status(0x01);
		break;
	case 0x08: case 0x0c:                          // characteristic module checksum
	case 0x28: case 0x2c: case 0x30: case 0x34:
	case 0x40: case 0x41: case 0x42: case 0x44: case 0x45: case 0x46:
	case 0x50: case 0x54: case 0x60: case 0x64:
		then_status((c & 0x04) ? 0x01 : 0x00);
		break;
	case 0x58: {                                   // power channel range test: report every range
		std::vector<u8> all;                       // at minimum, mid-scale and maximum
		for (int r = 0; r < 6; r++)
			for (u8 dac : {u8(0x00), u8(0x80), u8(0xff)}) {
				if (r == 5 && dac) break;          // the fixed 5 V channel: one point
				auto rec = sense_record(r, dac); all.insert(all.end(), rec.begin(), rec.end());
			}
		then_status(0x41);
		then_send(all);
		then_status(0x01);
		break;
	}
	case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: {
		// power channel command: parameters = mode, D/A value. Modes (PROM.TST option 7):
		// 01 on + set D/A + sense, 02 on, 04 set D/A, 08 sense, 10 sense continuously, FF off
		then_status(0x61);
		then_params(2);
		then_call([](prom_programmer &p) {
			int r = p.m_cmd - 0x90; if (r > 5) r = 5;
			u8 mode = p.params[0], dac = p.params[1];
			psu_state &s = p.m_psu[r];
			if (mode == 0xff) { s.on = false; p.then_status(0x01); return; }
			if (mode & 0x03) s.on = true;
			if (mode & 0x05) s.dac = dac;
			if (mode == 0x02) { p.then_status(0x01); return; }
			p.then_status(0x41);
			p.then_send(sense_record(r, s.on ? s.dac : 0));
			p.then_status(0x01);
		});
		break;
	}
	case 0x70: case 0x71: case 0x72: case 0x73: {
		// power channel level: parameters = requested voltage (sixteenths in the high nibble,
		// then integer volts); the controller picks the D/A value and reports one sense record
		then_status(0x61);
		then_params(2);
		then_call([](prom_programmer &p) {
			int r = p.m_cmd - 0x70;
			const psu_range &pr = PSU[r];
			double v = p.params[1] + (p.params[0] >> 4) / 16.0;   // fraction byte first (PROM.TST 264E, 264D)
			if (pr.vmax < 0) v = -v;
			double f = (v - pr.vmin) / (pr.vmax - pr.vmin);
			if (f < 0 || f > 1) { p.then_status(0x02); return; }   // out of range: failure
			p.m_psu[r].on = true; p.m_psu[r].dac = u8(f * 255.0 + 0.5);
			p.then_status(0x41);
			p.then_send(sense_record(r, p.m_psu[r].dac));
			p.then_status(0x01);
		});
		break;
	}
	case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: {
		// power channel evaluation: parameters = status display (FF on), 1st and 2nd D/A
		// value; the controller sweeps between them reporting sense records
		then_status(0x61);
		then_params(3);
		then_call([](prom_programmer &p) {
			int r = p.m_cmd - 0x80;
			u8 a = p.params[1], b = p.params[2];
			std::vector<u8> all;
			int step = a <= b ? 1 : -1;
			for (int d = a;; d += step) {
				if (p.params[0] == 0xff || d == a || d == b) { auto rec = sense_record(r, u8(d)); all.insert(all.end(), rec.begin(), rec.end()); }
				if (d == b) break;
			}
			p.m_psu[r].on = true; p.m_psu[r].dac = b;
			p.then_status(0x41);
			p.then_send(all);
			p.then_status(0x01);
		});
		break;
	}
	case 0xc0:                                     // status: 11 bytes, then done
		// 0 error code; 1 fatal controller errors (bits 6-4: 7 CPU, 6 CPU related, 5 ROM,
		// 4 system interface, 3 A/D at power-up, 2 power control shutdown, 1 configuration
		// supply); 2 power/device errors (linearity, power failure, device not erased...);
		// 3-10 the last power-channel sense record / error addresses. Healthy: all zero.
		then_status(0x41);
		then_send({0x00, 0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0});
		then_status(0x01);
		break;
	case 0xd0:                                     // characteristic module information (see module_info)
		// PROM.TST option 13 reads the 10-byte header only and ends with B0.
		then_status(0x41);
		then_send(module_info());
		then_status(0x01);
		break;
	case 0x38:                                     // write-PROM diagnostic for device index n
		then_status(0x61);
		then_params(1);
		then_call([](prom_programmer &p) {
			if (p.params[0] < 1 || p.params[0] > MODULE_DEVICES) { p.then_status(0x02); return; }
			p.then_status(0x01);                   // executing (writes a test pattern) until aborted
		});
		break;
	default:
		log("unknown command %02x", c);
		then_status(0x80);                         // bit 7: unrecognized command
		break;
	}
	advance();
}
