// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// Flexible disc media: building tracks from a linear image and decoding them back must be
// byte-exact for all four 8501 geometries; IMD images given as arguments must survive
// tracks and back sector for sector (IDs, deleted marks, CRC errors, missing data).
// Run with "make test".
#include "machine/flexdrive.h"
#include <cstdio>
#include <cstring>

static bool same(const std::vector<imd_track> &a, const std::vector<imd_track> &b, std::string &why)
{
	if (a.size() != b.size()) { why = "track count"; return false; }
	for (size_t t = 0; t < a.size(); t++) {
		const imd_track &x = a[t], &y = b[t];
		if (x.cyl != y.cyl || x.head != y.head || x.mfm() != y.mfm() || x.secs.size() != y.secs.size()) { why = "track " + std::to_string(t); return false; }
		for (size_t s = 0; s < x.secs.size(); s++) {
			const imd_sector &p = x.secs[s], &q = y.secs[s];
			if (p.cyl != q.cyl || p.head != q.head || p.id != q.id || p.size != q.size || p.deleted != q.deleted
				|| p.bad_crc != q.bad_crc || p.no_data != q.no_data || (!p.no_data && p.data != q.data)) {
				why = "track " + std::to_string(t) + " sector " + std::to_string(p.id); return false;
			}
		}
	}
	return true;
}

static bool imd_round_trip(const std::vector<imd_track> &in, std::string &why)
{
	flex::drive d; d.load_imd(in);
	std::vector<imd_track> out; d.to_imd(out);
	return same(in, out, why);
}

int main(int argc, char **argv) {
	int fails = 0;
	for (int k = 0; k < 4; k++) {
		fd_geometry g; g.ds = k & 2; g.dd = k & 1;
		std::vector<u8> lin(size_t(g.records()) * 256);
		for (size_t i = 0; i < lin.size(); i++) lin[i] = u8(i * 7 + i / 251);
		flex::drive d; d.load_linear(lin, g);
		std::vector<u8> back; fd_geometry g2; int bad = d.to_linear(back, g2);
		bool ok = !bad && back == lin && g2.ds == g.ds && g2.dd == g.dd;
		printf("%s: records %d, bad %d, %s\n", g.name(), g.records(), bad, ok ? "round trip OK" : "MISMATCH");
		fails += !ok;
	}
	// marks a DOS/50 disc never has: deleted data, a CRC error, a missing data field,
	// a foreign ID and a short track
	{
		std::vector<imd_track> v(2);
		v[0].mode = 0; v[0].cyl = 0; v[0].head = 0;
		v[1].mode = 3; v[1].cyl = 1; v[1].head = 1;
		for (int s = 1; s <= 26; s++) { imd_sector x; x.cyl = 0; x.head = 0; x.id = u8(s); x.size = 0; x.data.assign(128, u8(s)); v[0].secs.push_back(x); }
		for (int s = 1; s <= 9; s++) { imd_sector x; x.cyl = 1; x.head = 1; x.id = u8(s); x.size = 1; x.data.assign(256, u8(0x40 + s)); v[1].secs.push_back(x); }
		v[0].secs[2].deleted = true; v[0].secs[5].bad_crc = true; v[0].secs[7].deleted = v[0].secs[7].bad_crc = true;
		v[0].secs[9].no_data = true; v[0].secs[9].data.clear();
		v[1].secs[3].cyl = 9; v[1].secs[4].head = 0; v[1].secs[4].deleted = true;
		std::string why; bool ok = imd_round_trip(v, why);
		printf("marks and odd layouts: %s\n", ok ? "round trip OK" : ("MISMATCH at " + why).c_str());
		fails += !ok;
	}
	for (int a = 1; a < argc; a++) {
		std::vector<u8> raw; std::vector<imd_track> trks; std::string err, why;
		if (FILE *f = fopen(argv[a], "rb")) { int c; while ((c = fgetc(f)) != EOF) raw.push_back(u8(c)); fclose(f); }
		bool ok = parse_imd_tracks(raw, trks, err) && imd_round_trip(trks, why);
		const char *base = strrchr(argv[a], '/');
		printf("%s: %s\n", base ? base + 1 : argv[a], ok ? "round trip OK" : ("FAILED " + err + why).c_str());
		fails += !ok;
	}
	return fails;
}
