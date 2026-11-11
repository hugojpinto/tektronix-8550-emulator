#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
"""Rebuild disks/System_helpfix.img from the original sources/bitsavers/disks/System.imd:
the IMD as a raw linear image (record N at N*256), with HELP records 3854-3855
restored from the identical older copy at 1474-1475 (see disks/README.md).
usage: tools/make_system_helpfix.py [OUTPUT]"""
import os, sys
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
from imd import parse
root = os.path.join(here, '..')
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, 'disks', 'System_helpfix.img')
_, tracks = parse(os.path.join(root, 'sources', 'bitsavers', 'disks', 'System.imd'))
raw = bytearray()
for t in tracks:
    for s in sorted(t['secs']): raw += t['secs'][s] or b'\xe5' * t['bs']
assert len(raw) == 3991 * 256, len(raw)
raw[3854 * 256:3856 * 256] = raw[1474 * 256:1476 * 256]
with open(out, 'wb') as f: f.write(raw)
print(out, len(raw))
