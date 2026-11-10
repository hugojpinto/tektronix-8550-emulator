// Prototype (target system) reached over IPC -- protocol in notes/prototype-link.md.
#ifndef TEK_HOST_IPC_TARGET_H
#define TEK_HOST_IPC_TARGET_H
#include "machine/prototype.h"
#include <string>

class ipc_target : public prototype_bus
{
public:
	// spec: "unix:/path/to/socket" or "tcp:host:port"
	bool open(const std::string &spec, std::string &err);
	~ipc_target() override;

	bool connected() const override { return m_fd >= 0; }
	u8   mem_read(u16 a) override { return req('R', a, 0); }
	u8   mem_fetch(u16 a) override { return req(m_version >= 2 ? 'F' : 'R', a, 0); }
	void mem_write(u16 a, u8 d) override { req('W', a, d); }
	u8   io_read(u16 port) override { return req('I', port, 0); }   // port A8-A15 in addr_hi
	void io_write(u16 port, u8 d) override { req('O', port, d); }
	bool irq() override { return m_lines & 1; }
	bool nmi() override { return m_lines & 2; }
	u8   irq_vector() override { return req('V', 0, 0); }
	void reset() override { req('Z', 0, 0); }
	void poll(unsigned long z80_cycles) override;
	int version() const { return m_version; }
	static constexpr double Z80_HZ = 4'000'000.0;   // emulator processor clock
	unsigned long requests() const { return m_requests; }

private:
	int m_fd = -1;
	u8 m_lines = 0;
	unsigned m_poll = 0;
	int m_version = 1;
	bool m_cyc_valid = false;
	unsigned long m_cyc_last = 0;
	double m_us_frac = 0;
	unsigned long m_requests = 0;
	u8 req(char op, u16 a, u8 d);
};
#endif
