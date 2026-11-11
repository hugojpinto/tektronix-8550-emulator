// The CT8500's raster: characters on a 6x8 dot matrix in an 8x10 dot field
// (CT8500 Operators Manual 070-3737-00), 80x25 -> 640 dots x 250 scan lines.
// Front ends scale and colour it; nothing here depends on a graphics library.
#ifndef TEK_HOST_CT8500_RASTER_H
#define TEK_HOST_CT8500_RASTER_H
#include "host/ct8500.h"
#include <cstdint>

namespace ct8500_raster {
constexpr int W = ct8500::COLS * 8, H = ct8500::VIEW * 10;
// dots[W*H]: 0 off, 1 dim, 2 bright. blink_on: blinking characters are lit in this
// phase. The cursor is a steady block in inverse video, as in the screenshots.
void draw(const ct8500 &t, bool blink_on, uint8_t *dots);
}
#endif
