// CT8500 raster -- see ct8500_raster.h
#include "host/ct8500_raster.h"
#include <cstring>

namespace {
const uint8_t font[96][8] = {
#include "host/ct8500_font.inc"
};
}

void ct8500_raster::draw(const ct8500 &t, bool blink_on, uint8_t *dots)
{
	memset(dots, 0, size_t(W) * H);
	const int cr = t.cursor_visible() ? t.cursor_row() : -1, cc = t.cursor_col();
	for (int y = 0; y < ct8500::VIEW; y++)
		for (int x = 0; x < ct8500::COLS; x++) {
			uint8_t a = t.attrs(y, x), ch = t.cell(y, x);
			uint8_t lvl = (a & ct8500::A_DIM) ? 1 : 2;
			uint8_t cell[10][8] = {};
			bool shown = !(a & ct8500::A_BLANK) && (!(a & ct8500::A_BLINK) || blink_on);
			if (shown && ch >= 0x20 && ch < 0x80) {
				const uint8_t *g = font[ch - 0x20];
				for (int gy = 0; gy < 8; gy++)            // 5 dots + spacing at field rows 1-8
					for (int gx = 0; gx < 5; gx++)
						if (g[gy] >> (4 - gx) & 1) cell[1 + gy][1 + gx] = lvl;
			}
			if (shown && (a & ct8500::A_UL)) for (int cx = 0; cx < 8; cx++) cell[9][cx] = lvl;
			bool inv = bool(a & ct8500::A_INV) ^ (y == cr && x == cc);
			uint8_t *row0 = dots + size_t(y * 10) * W + x * 8;
			for (int cy = 0; cy < 10; cy++)
				for (int cx = 0; cx < 8; cx++) {
					uint8_t v = cell[cy][cx];
					if (inv) v = v ? 0 : lvl;
					row0[size_t(cy) * W + cx] = v;
				}
		}
}
