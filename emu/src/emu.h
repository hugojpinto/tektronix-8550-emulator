// Minimal stand-in for MAME's emu.h, so BSD-3-Clause MAME CPU/disassembler sources
// can be compiled unchanged (or nearly) outside the MAME framework.
#ifndef TEK_EMU_SHIM_H
#define TEK_EMU_SHIM_H
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cassert>
#include <string>
#include <ostream>
#include <functional>
#include <type_traits>

using u8 = uint8_t;  using u16 = uint16_t; using u32 = uint32_t; using u64 = uint64_t;
using s8 = int8_t;   using s16 = int16_t;  using s32 = int32_t;  using s64 = int64_t;
using offs_t = uint32_t;

#define ATTR_COLD
enum { CLEAR_LINE = 0, ASSERT_LINE = 1 };

template <typename T> constexpr T BIT(T x, int n) { return (x >> n) & T(1); }
template <typename T> constexpr T BIT(T x, int n, int w) { return (x >> n) & ((T(1) << w) - 1); }
template <typename T, typename... B> constexpr T bitswap(T val, B... b) {
	T r = 0; int n = sizeof...(b); int bits[] = { int(b)... };
	for (int i = 0; i < n; i++) r |= BIT(val, bits[i]) << (n - 1 - i);
	return r;
}
template <unsigned W, typename T, typename... B> constexpr T bitswap(T val, B... b) { return bitswap<T>(val, b...); }

union PAIR {
	struct { u8 l, h, h2, h3; } b;
	struct { u16 l, h; } w;
	struct { s8 l, h, h2, h3; } sb;
	struct { s16 l, h; } sw;
	u32 d; s32 sd;
};

namespace util {
	namespace detail {
		template <typename T> inline auto fmt_arg(const T &v) {
			if constexpr (std::is_same_v<std::decay_t<T>, std::string>) return v.c_str();
			else return v;
		}
		inline std::string vfmt(const char *f, ...) {
			char buf[512]; va_list ap; va_start(ap, f); vsnprintf(buf, sizeof buf, f, ap); va_end(ap); return buf;
		}
	}
	template <typename... A> std::string string_format(const char *f, const A &... a) { return detail::vfmt(f, detail::fmt_arg(a)...); }
	template <typename... A> void stream_format(std::ostream &s, const char *f, const A &... a) { s << string_format(f, a...); }
	template <typename T> constexpr std::make_signed_t<T> sext(T v, int bits) {
		using S = std::make_signed_t<T>; return S(v << (8*sizeof(T) - bits)) >> (8*sizeof(T) - bits);
	}

	class disasm_interface {
	public:
		enum : u32 {
			LENGTHMASK = 0x0000ffff, SUPPORTED = 0x80000000,
			STEP_OUT = 0x40000000, STEP_OVER = 0x20000000, STEP_COND = 0x10000000,
			STEP_OVER_EXTRA_MASK = 0x0f000000, PAGED = 0x00000008
		};
		// Byte-addressed little-endian reader over the target's memory.
		class data_buffer {
		public:
			std::function<u8(offs_t)> rd;
			u8  r8(offs_t a)  const { return rd(a); }
			u16 r16(offs_t a) const { return u16(rd(a)) | (u16(rd(a + 1)) << 8); }
			u32 r32(offs_t a) const { return u32(r16(a)) | (u32(r16(a + 2)) << 16); }
		};
		virtual ~disasm_interface() = default;
		virtual u32 opcode_alignment() const = 0;
		virtual u32 interface_flags() const { return 0; }
		virtual u32 page_address_bits() const { return 0; }
		virtual offs_t disassemble(std::ostream &stream, offs_t pc, const data_buffer &opcodes, const data_buffer &params) = 0;
	};
}
using data_buffer = util::disasm_interface::data_buffer;
#endif
