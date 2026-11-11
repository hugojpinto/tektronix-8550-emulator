// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// Flexible disc media: building tracks from a linear image and decoding them back must be
// byte-exact for all four 8501 geometries. Run with "make test".
#include "machine/flexdrive.h"
#include <cstdio>
int main() {
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
	return fails;
}
