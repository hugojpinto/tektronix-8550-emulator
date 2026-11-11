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


// 8501 flexible disc geometry (061-2355-00 service manual, controller chapter):
// IBM-compatible 8" media, 77 cylinders, 1 or 2 sides, 26 sectors per track.
// Single density (FM) has 128-byte sectors; double density (MFM) has 256-byte
// sectors, except track 0 (cylinder 0, side 0), which is always FM. The controller
// counts 256-byte records: 13 per FM track, 26 per MFM track. Logical track t is
// cylinder t/2, side t%2 on a two-sided disc. Capacities: SSSD 1001, SSDD 1989,
// DSSD 2002, DSDD 3991 records -- all different, so an image's size gives its geometry.
struct fd_geometry {
	bool ds = true, dd = true;
	int  tracks() const { return ds ? 154 : 77; }
	bool fm(int t) const { return !dd || t == 0; }
	int  track_records(int t) const { return fm(t) ? 13 : 26; }
	int  track_first(int t) const { return t == 0 ? 0 : dd ? 13 + 26 * (t - 1) : 13 * t; }
	int  records() const { return track_first(tracks()); }
	int  cylinder(int t) const { return ds ? t / 2 : t; }
	int  side(int t) const { return ds ? t % 2 : 0; }
	const char *name() const { return ds ? (dd ? "DSDD" : "DSSD") : (dd ? "SSDD" : "SSSD"); }
	static bool from_records(size_t n, fd_geometry &g)
	{
		for (int k = 0; k < 4; k++) {
			fd_geometry c; c.ds = k & 2; c.dd = k & 1;
			if (size_t(c.records()) == n) { g = c; return true; }
		}
		return false;
	}
};

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

// Write a linear 8501 image as IMD in the 8501's physical format for its geometry:
// per track 26 sectors, 128-byte FM or 256-byte MFM (500 kbps). Atomic: temp file + rename.
inline bool save_imd_8501(const std::string &path, const std::vector<u8> &lin, const fd_geometry &g, const std::string &comment, std::string &err)
{
	std::string tmp = path + ".tmp";
	FILE *f = fopen(tmp.c_str(), "wb");
	if (!f) { err = "cannot write " + tmp; return false; }
	time_t now = time(nullptr); struct tm *t = localtime(&now);
	fprintf(f, "IMD 1.18: %02d/%02d/%04d %02d:%02d:%02d\r\n%s", t->tm_mday, t->tm_mon + 1, t->tm_year + 1900, t->tm_hour, t->tm_min, t->tm_sec, comment.c_str());
	fputc(0x1a, f);
	for (int trk = 0; trk < g.tracks(); trk++) {
		bool fm = g.fm(trk);
		size_t bs = fm ? 128 : 256, off = size_t(g.track_first(trk)) * 256;
		fputc(fm ? 0 : 3, f); fputc(g.cylinder(trk), f); fputc(g.side(trk), f); fputc(26, f); fputc(fm ? 0 : 1, f);
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
#endif
