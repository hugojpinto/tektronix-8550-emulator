// ImageDisk (.IMD) -> linear sector image, in physical order:
// cylinder, head, sector id ascending (8501 records are laid out this way).
#ifndef TEK_IMD_H
#define TEK_IMD_H
#include "emu.h"
#include <vector>
#include <string>
#include <map>
#include <cstdio>
#include <ctime>
#include <algorithm>

inline bool load_imd_linear(const std::string &path, std::vector<u8> &out, std::string &err, bool *is_imd = nullptr, std::string *comment = nullptr)
{
	FILE *f = fopen(path.c_str(), "rb");
	if (!f) { err = "cannot open " + path; return false; }
	std::vector<u8> d; int c; while ((c = fgetc(f)) != EOF) d.push_back(u8(c)); fclose(f);
	if (d.size() < 4 || d[0] != 'I' || d[1] != 'M' || d[2] != 'D') {
		// not IMD: accept a raw linear image as-is
		if (is_imd) *is_imd = false;
		out = d; return true;
	}
	size_t i = 0; while (i < d.size() && d[i] != 0x1a) i++;
	if (is_imd) *is_imd = true;
	if (comment) {   // keep the text after the IMD header line
		std::string all(d.begin(), d.begin() + i);
		size_t nl = all.find('\n'); *comment = nl == std::string::npos ? "" : all.substr(nl + 1);
	}
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

// Write a linear 8501 image (3991 x 256-byte records) as IMD in the 8501's
// physical format: 77 cyl x 2 heads x 26 sectors; cyl 0 head 0 = 128 B FM,
// everything else 256 B MFM (500 kbps). Atomic: temp file + rename.
inline bool save_imd_8501(const std::string &path, const std::vector<u8> &lin, const std::string &comment, std::string &err)
{
	std::string tmp = path + ".tmp";
	FILE *f = fopen(tmp.c_str(), "wb");
	if (!f) { err = "cannot write " + tmp; return false; }
	time_t now = time(nullptr); struct tm *t = localtime(&now);
	fprintf(f, "IMD 1.18: %02d/%02d/%04d %02d:%02d:%02d\r\n%s", t->tm_mday, t->tm_mon + 1, t->tm_year + 1900, t->tm_hour, t->tm_min, t->tm_sec, comment.c_str());
	fputc(0x1a, f);
	size_t off = 0;
	for (int cyl = 0; cyl < 77; cyl++)
		for (int head = 0; head < 2; head++) {
			bool fm = (cyl == 0 && head == 0);
			size_t bs = fm ? 128 : 256;
			fputc(fm ? 0 : 3, f); fputc(cyl, f); fputc(head, f); fputc(26, f); fputc(fm ? 0 : 1, f);
			for (int s = 1; s <= 26; s++) fputc(s, f);
			for (int s = 0; s < 26; s++) {
				const u8 *p = off + bs <= lin.size() ? &lin[off] : nullptr;
				bool same = p && std::all_of(p, p + bs, [&](u8 b) { return b == p[0]; });
				if (same) { fputc(2, f); fputc(p[0], f); }        // compressed
				else { fputc(1, f); for (size_t i = 0; i < bs; i++) fputc(p ? p[i] : 0xe5, f); }
				off += bs;
			}
		}
	bool ok = !ferror(f);
	fclose(f);
	if (!ok || rename(tmp.c_str(), path.c_str()) != 0) { err = "cannot replace " + path; return false; }
	return true;
}

inline bool save_raw(const std::string &path, const std::vector<u8> &lin, std::string &err)
{
	std::string tmp = path + ".tmp";
	FILE *f = fopen(tmp.c_str(), "wb");
	if (!f) { err = "cannot write " + tmp; return false; }
	bool ok = fwrite(lin.data(), 1, lin.size(), f) == lin.size();
	fclose(f);
	if (!ok || rename(tmp.c_str(), path.c_str()) != 0) { err = "cannot replace " + path; return false; }
	return true;
}
