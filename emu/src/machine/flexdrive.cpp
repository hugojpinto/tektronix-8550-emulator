// 8" flexible disc drive and media -- see flexdrive.h and notes/8501-fdc.md
#include "machine/flexdrive.h"
#include <algorithm>

namespace flex {

namespace {
struct writer {
	track &t; size_t pos = 0; u16 crc = 0xffff;
	explicit writer(track &tt) : t(tt) {}
	void put(u8 b, u8 m = M_NONE) { if (pos < t.data.size()) { t.data[pos] = b; t.mark[pos] = m; } pos++; crc = crc16(crc, b); }
	void fill(u8 b, int n) { while (n-- > 0) put(b); }
	void preset() { crc = 0xffff; }
	void put_crc() { u16 c = crc; put(u8(c >> 8)); put(u8(c)); }
};
}

void build_track(track &t, bool mfm, int cyl, int side, const std::vector<const u8 *> &secs)
{
	t.erase(mfm);
	writer w(t);
	const int ssize = mfm ? 256 : 128;
	if (mfm) {
		w.fill(0x4e, 80); w.fill(0x00, 12);
		for (int i = 0; i < 3; i++) w.put(0xc2, M_MFM_C2);
		w.put(0xfc); w.fill(0x4e, 50);
	} else {
		w.fill(0xff, 40); w.fill(0x00, 6);
		w.put(0xfc, M_FM_D7); w.fill(0xff, 26);
	}
	for (int s = 1; s <= 26; s++) {
		// ID field
		if (mfm) { w.fill(0x00, 12); w.preset(); for (int i = 0; i < 3; i++) w.put(0xa1, M_MFM_A1); w.put(0xfe); }
		else { w.fill(0x00, 6); w.preset(); w.put(0xfe, M_FM_C7); }
		w.put(u8(cyl)); w.put(u8(side)); w.put(u8(s)); w.put(mfm ? 1 : 0); w.put_crc();
		w.fill(mfm ? 0x4e : 0xff, mfm ? 22 : 11);
		// data field
		if (mfm) { w.fill(0x00, 12); w.preset(); for (int i = 0; i < 3; i++) w.put(0xa1, M_MFM_A1); w.put(0xfb); }
		else { w.fill(0x00, 6); w.preset(); w.put(0xfb, M_FM_C7); }
		const u8 *p = size_t(s - 1) < secs.size() ? secs[s - 1] : nullptr;
		for (int i = 0; i < ssize; i++) w.put(p ? p[i] : 0xe5);
		w.put_crc();
		w.fill(mfm ? 0x4e : 0xff, mfm ? 54 : 27);
	}
}

bool decode_track(const track &t, int cyl, int side, int ssize, std::vector<std::vector<u8>> &secs, int *bad)
{
	secs.assign(26, std::vector<u8>());
	if (!t.formatted()) { if (bad) *bad += 26; return false; }
	const size_t n = t.data.size();
	auto at = [&](size_t i) { return t.data[i % n]; };
	auto mk = [&](size_t i) { return t.mark[i % n]; };
	const u8 sync = t.mfm ? M_MFM_A1 : M_FM_C7;
	// a field starts at a sync mark: MFM A1 A1 A1 + mark byte, FM the mark byte itself
	auto field = [&](size_t i, size_t &body, u16 &crc) -> int {
		crc = 0xffff;
		if (t.mfm) {
			if (mk(i) != sync || mk(i + 1) != sync || mk(i + 2) != sync) return -1;
			for (int k = 0; k < 3; k++) crc = crc16(crc, at(i + k));
			i += 3;
		} else if (mk(i) != sync) return -1;
		crc = crc16(crc, at(i));
		body = i + 1;
		return at(i);
	};
	int good = 0;
	for (size_t i = 0; i < n; i++) {
		size_t body; u16 crc;
		if (field(i, body, crc) != 0xfe) continue;
		u8 id[6]; for (int k = 0; k < 6; k++) { id[k] = at(body + k); crc = crc16(crc, id[k]); }
		if (crc != 0 || id[0] != cyl || id[1] != side || id[2] < 1 || id[2] > 26 || (128 << (id[3] & 3)) != ssize) continue;
		// the data field follows within the gap
		for (size_t j = body + 6; j < body + 6 + 80; j++) {
			size_t db; u16 dcrc;
			int m = field(j, db, dcrc);
			if (m < 0) continue;
			if (m != 0xfb && m != 0xf8) break;
			std::vector<u8> d(static_cast<size_t>(ssize));
			for (int k = 0; k < ssize; k++) { d[k] = at(db + k); dcrc = crc16(dcrc, d[k]); }
			dcrc = crc16(crc16(dcrc, at(db + ssize)), at(db + ssize + 1));
			if (dcrc == 0 && secs[id[2] - 1].empty()) { secs[id[2] - 1] = d; good++; }
			break;
		}
		i = body;
	}
	if (bad) *bad += 26 - good;
	return good == 26;
}

void drive::load_unformatted(bool ds)
{
	tracks.assign((MAX_CYL + 1) * 2, track());
	two_sided = ds;
	present = true; disc_change = true; dirty = false;
}

void drive::load_linear(const std::vector<u8> &lin, const fd_geometry &g)
{
	load_unformatted(g.ds);
	std::vector<u8> e5(256, 0xe5);
	for (int t = 0; t < g.tracks(); t++) {
		bool fm = g.fm(t);
		size_t bs = fm ? 128 : 256, off = size_t(g.track_first(t)) * 256;
		std::vector<const u8 *> secs;
		for (int s = 0; s < 26; s++, off += bs) secs.push_back(off + bs <= lin.size() ? &lin[off] : e5.data());
		build_track(tracks[size_t(g.cylinder(t) * 2 + g.side(t))], !fm, g.cylinder(t), g.side(t), secs);
	}
}

int drive::to_linear(std::vector<u8> &lin, fd_geometry &g) const
{
	g.ds = two_sided;
	const track &t1 = tracks[g.ds ? 1 : 2];   // logical track 1: cylinder 0 side 1, or cylinder 1
	g.dd = !t1.formatted() || t1.mfm;
	lin.assign(size_t(g.records()) * 256, 0xe5);
	int bad = 0;
	for (int t = 0; t < g.tracks(); t++) {
		bool fm = g.fm(t);
		size_t bs = fm ? 128 : 256, off = size_t(g.track_first(t)) * 256;
		const track &tr = tracks[size_t(g.cylinder(t) * 2 + g.side(t))];
		std::vector<std::vector<u8>> secs;
		if (tr.formatted() && tr.mfm == !fm) decode_track(tr, g.cylinder(t), g.side(t), int(bs), secs, &bad);
		else { bad += 26; secs.assign(26, std::vector<u8>()); }
		for (int s = 0; s < 26; s++, off += bs)
			if (!secs[s].empty()) std::copy(secs[s].begin(), secs[s].end(), lin.begin() + off);
	}
	return bad;
}

}
