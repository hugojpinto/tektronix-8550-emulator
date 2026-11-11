// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// 8" flexible disc drive (Qume DataTrak 8, as fitted to the 8501) and its media,
// modelled at the level the 8501 Flexible Disc Controller sees: a rotating track of
// byte cells. The controller's TTL serialises bytes, generates clocks and detects
// address marks, so the Z80 never sees flux; each cell holds a data byte and the
// kind of missing-clock mark it carries, if any. See notes/8501-fdc.md.
//
// Times are in controller Z80 cycles (4 MHz). One revolution (360 rpm) is
// ROT cycles: 5208 FM cells of 128 cycles (250 kbit/s) or 10416 MFM cells of 64.
#ifndef TEK_MACHINE_FLEXDRIVE_H
#define TEK_MACHINE_FLEXDRIVE_H
#include "emu.h"
#include "machine/imd.h"
#include <vector>
#include <string>

namespace flex {
constexpr u64 CLOCK = 4'000'000;
constexpr u64 ROT = 666'624;            // 166.66 ms
constexpr u64 INDEX_W = 8'000;          // index pulse, 2 ms
inline u64 cell_cycles(bool mfm) { return mfm ? 64 : 128; }
inline u32 track_cells(bool mfm) { return u32(ROT / cell_cycles(mfm)); }

enum mark_t : u8 { M_NONE = 0, M_FM_C7, M_FM_D7, M_FM_OTHER, M_MFM_A1, M_MFM_C2 };

inline u16 crc16(u16 crc, u8 b)          // CRC-CCITT, x^16 + x^12 + x^5 + 1
{
	crc ^= u16(b) << 8;
	for (int i = 0; i < 8; i++) crc = (crc & 0x8000) ? u16((crc << 1) ^ 0x1021) : u16(crc << 1);
	return crc;
}

struct track {
	bool mfm = false;
	std::vector<u8> data, mark;         // empty = never written (unformatted)
	bool formatted() const { return !data.empty(); }
	void erase(bool m) { mfm = m; data.assign(track_cells(m), m ? 0x4e : 0xff); mark.assign(data.size(), M_NONE); }
};

// IBM 3740 (FM) / System 34 (MFM) track, laid out exactly as the controller's own
// FORMAT tables write it (160-0412-00 at 0DD4 / 0E42). Sector n is secs[n-1].
void build_track(track &t, bool mfm, int cyl, int side, const std::vector<const u8 *> &secs);
// Any track as recorded (an IMD track): the sectors in order, with their ID fields,
// deleted-data (F8) marks, CRC errors and missing data fields; same gaps as above.
void build_track(track &t, bool mfm, const std::vector<imd_sector> &secs);
// Every sector found on a track, in physical order from the index: what an IMD records.
void read_track(const track &t, std::vector<imd_sector> &out);
// Decode the sectors of a track: id 1..26 -> data; false if any is missing, of the
// wrong size or has a bad CRC (problems are counted in *bad).
bool decode_track(const track &t, int cyl, int side, int ssize, std::vector<std::vector<u8>> &secs, int *bad);

class drive
{
public:
	// media
	bool present = false;                // disc in, door closed
	bool two_sided = true;
	bool write_protect = false;
	std::vector<track> tracks;           // [cyl * 2 + side]
	bool dirty = false; u64 last_write = 0;
	std::string path, comment; bool is_imd = false, writeback = false;

	// mechanism
	static constexpr int CYLS = 77, MAX_CYL = 79;
	int cyl = 0;
	bool disc_change = true;             // reported until the drive is next deselected
	bool head_loaded = false;

	// load a linear 8501 image (records of 256 bytes in physical order) of geometry g
	void load_linear(const std::vector<u8> &lin, const fd_geometry &g);
	void load_unformatted(bool ds);      // blank media
	// an IMD image, track by track (deleted marks, CRC errors and odd layouts survive)
	void load_imd(const std::vector<imd_track> &trks);
	void to_imd(std::vector<imd_track> &out) const;
	// back to a linear image; geometry from the media. Returns the number of
	// sectors that could not be decoded (filled with E5).
	int to_linear(std::vector<u8> &lin, fd_geometry &g) const;
	track &trk(int side) { return tracks[size_t(cyl * 2 + (side & 1))]; }
	bool ready() const { return present; }
	bool index(u64 now) const { return present && (now % ROT) < INDEX_W; }
	void step(bool in) { cyl = in ? std::min(cyl + 1, MAX_CYL) : std::max(cyl - 1, 0); }
};
}
#endif
