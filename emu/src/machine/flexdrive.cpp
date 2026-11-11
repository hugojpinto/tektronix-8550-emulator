// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
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

void build_track(track &t, bool mfm, const std::vector<imd_sector> &secs)
{
	t.erase(mfm);
	writer w(t);
	if (mfm) {
		w.fill(0x4e, 80); w.fill(0x00, 12);
		for (int i = 0; i < 3; i++) w.put(0xc2, M_MFM_C2);
		w.put(0xfc); w.fill(0x4e, 50);
	} else {
		w.fill(0xff, 40); w.fill(0x00, 6);
		w.put(0xfc, M_FM_D7); w.fill(0xff, 26);
	}
	for (const imd_sector &s : secs) {
		// ID field
		if (mfm) { w.fill(0x00, 12); w.preset(); for (int i = 0; i < 3; i++) w.put(0xa1, M_MFM_A1); w.put(0xfe); }
		else { w.fill(0x00, 6); w.preset(); w.put(0xfe, M_FM_C7); }
		w.put(s.cyl); w.put(s.head); w.put(s.id); w.put(s.size); w.put_crc();
		w.fill(mfm ? 0x4e : 0xff, mfm ? 22 : 11);
		// data field: FB, or F8 for a deleted-data (control) record; none if never recorded
		const size_t n = size_t(128) << (s.size & 7);
		if (s.no_data) w.fill(mfm ? 0x4e : 0xff, int(n) + (mfm ? 18 : 9));
		else {
			u8 m = s.deleted ? 0xf8 : 0xfb;
			if (mfm) { w.fill(0x00, 12); w.preset(); for (int i = 0; i < 3; i++) w.put(0xa1, M_MFM_A1); w.put(m); }
			else { w.fill(0x00, 6); w.preset(); w.put(m, M_FM_C7); }
			for (size_t i = 0; i < n; i++) w.put(i < s.data.size() ? s.data[i] : 0xe5);
			if (s.bad_crc) { u16 c = u16(w.crc ^ 0xffff); w.put(u8(c >> 8)); w.put(u8(c)); }
			else w.put_crc();
		}
		w.fill(mfm ? 0x4e : 0xff, mfm ? 54 : 27);
	}
}

void build_track(track &t, bool mfm, int cyl, int side, const std::vector<const u8 *> &secs)
{
	std::vector<imd_sector> v(26);
	const int ssize = mfm ? 256 : 128;
	for (int s = 1; s <= 26; s++) {
		imd_sector &x = v[size_t(s - 1)];
		x.cyl = u8(cyl); x.head = u8(side); x.id = u8(s); x.size = mfm ? 1 : 0;
		const u8 *p = size_t(s - 1) < secs.size() ? secs[size_t(s - 1)] : nullptr;
		x.data.assign(size_t(ssize), 0xe5);
		if (p) std::copy(p, p + ssize, x.data.begin());
	}
	build_track(t, mfm, v);
}

void read_track(const track &t, std::vector<imd_sector> &out)
{
	out.clear();
	if (!t.formatted()) return;
	const size_t n = t.data.size();
	auto at = [&](size_t i) { return t.data[i % n]; };
	auto mk = [&](size_t i) { return t.mark[i % n]; };
	const u8 sync = t.mfm ? M_MFM_A1 : M_FM_C7;
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
	for (size_t i = 0; i < n; i++) {
		size_t body; u16 crc;
		if (field(i, body, crc) != 0xfe) continue;
		u8 id[6]; for (int k = 0; k < 6; k++) { id[k] = at(body + k); crc = crc16(crc, id[k]); }
		if (crc != 0) continue;                       // a damaged ID is not a sector
		imd_sector s; s.cyl = id[0]; s.head = id[1]; s.id = id[2]; s.size = id[3] & 7; s.no_data = true;
		const size_t ssize = size_t(128) << s.size;
		for (size_t j = body + 6; j < body + 6 + 80; j++) {
			size_t db; u16 dcrc;
			int m = field(j, db, dcrc);
			if (m < 0) continue;
			if (m != 0xfb && m != 0xf8) break;
			s.data.resize(ssize);
			for (size_t k = 0; k < ssize; k++) { s.data[k] = at(db + k); dcrc = crc16(dcrc, s.data[k]); }
			dcrc = crc16(crc16(dcrc, at(db + ssize)), at(db + ssize + 1));
			s.no_data = false; s.deleted = m == 0xf8; s.bad_crc = dcrc != 0;
			break;
		}
		out.push_back(s);
		i = body;
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

void drive::load_imd(const std::vector<imd_track> &trks)
{
	bool ds = false;
	for (const imd_track &t : trks) ds |= t.head == 1;
	load_unformatted(ds);
	for (const imd_track &t : trks)
		if (t.cyl <= MAX_CYL) build_track(tracks[size_t(t.cyl * 2 + (t.head & 1))], t.mfm(), t.secs);
}

void drive::to_imd(std::vector<imd_track> &out) const
{
	out.clear();
	for (int c = 0; c <= MAX_CYL; c++)
		for (int h = 0; h < (two_sided ? 2 : 1); h++) {
			const track &tr = tracks[size_t(c * 2 + h)];
			if (!tr.formatted()) continue;
			imd_track t; t.cyl = u8(c); t.head = u8(h); t.mode = tr.mfm ? 3 : 0;
			read_track(tr, t.secs);
			// IMD has one sector size per track: keep the sectors of the first one's size
			if (!t.secs.empty()) {
				u8 sz = t.secs[0].size;
				t.secs.erase(std::remove_if(t.secs.begin(), t.secs.end(), [&](const imd_sector &x) { return x.size != sz; }), t.secs.end());
			}
			if (!t.secs.empty()) out.push_back(t);
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
