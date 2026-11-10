// ImageDisk (.IMD) -> linear sector image, in physical order:
// cylinder, head, sector id ascending (8501 records are laid out this way).
#ifndef TEK_IMD_H
#define TEK_IMD_H
#include "emu.h"
#include <vector>
#include <string>
#include <map>
#include <cstdio>

inline bool load_imd_linear(const std::string &path, std::vector<u8> &out, std::string &err)
{
	FILE *f = fopen(path.c_str(), "rb");
	if (!f) { err = "cannot open " + path; return false; }
	std::vector<u8> d; int c; while ((c = fgetc(f)) != EOF) d.push_back(u8(c)); fclose(f);
	if (d.size() < 4 || d[0] != 'I' || d[1] != 'M' || d[2] != 'D') {
		// not IMD: accept a raw linear image as-is
		out = d; return true;
	}
	size_t i = 0; while (i < d.size() && d[i] != 0x1a) i++;
	i++;
	std::map<int, std::vector<u8>> tracks;   // key = cyl*2+head
	while (i + 5 <= d.size()) {
		u8 cyl = d[i+1], head = d[i+2], ns = d[i+3], sz = d[i+4]; i += 5;
		std::vector<u8> smap(d.begin() + i, d.begin() + i + ns); i += ns;
		if (head & 0x80) i += ns;
		if (head & 0x40) i += ns;
		size_t bs = size_t(128) << sz;
		std::map<int, std::vector<u8>> secs;
		for (u8 s : smap) {
			u8 t = d[i++];
			std::vector<u8> data(bs, 0xe5);
			if (t == 0) {}
			else if (t == 2 || t == 4 || t == 6 || t == 8) { std::fill(data.begin(), data.end(), d[i]); i++; }
			else { std::copy(d.begin() + i, d.begin() + i + bs, data.begin()); i += bs; }
			secs[s] = data;
		}
		std::vector<u8> trk;
		for (auto &kv : secs) trk.insert(trk.end(), kv.second.begin(), kv.second.end());
		tracks[cyl * 2 + (head & 1)] = trk;
	}
	out.clear();
	for (auto &kv : tracks) out.insert(out.end(), kv.second.begin(), kv.second.end());
	return true;
}
#endif
