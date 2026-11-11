#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
# The smallest useful 8550 target system (prototype link v3, notes/prototype-link.md):
# 64 KB of RAM and a console output port at 10h. Example for docs/INTEGRATION.md.
# usage: tools/mini_target.py PORT   then   tek8550 -target tcp:127.0.0.1:PORT ...
import socket, sys
ram = bytearray(64 * 1024)
srv = socket.socket(); srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(('127.0.0.1', int(sys.argv[1]))); srv.listen(1)
conn, _ = srv.accept()
buf, ver = b'', 1
while True:
    data = conn.recv(4096)
    if not data: break
    buf += data
    out = bytearray()
    while len(buf) >= 4:                      # requests are always 4 bytes
        op, lo, hi, d = buf[0], buf[1], buf[2], buf[3]; buf = buf[4:]
        a, res, lines = lo | hi << 8, 0xff, 0
        if op == ord('H'):   res = ver = max(1, min(a, 3))   # agree on a version <= the emulator's
        elif op in (ord('R'), ord('F')): res = ram[a]       # memory read / opcode fetch (M1)
        elif op == ord('W'): ram[a] = d                     # memory write
        elif op == ord('I'): res = 0x02 if lo == 0x11 else 0  # port 11h: "transmitter ready"
        elif op == ord('O') and lo == 0x10: print(chr(d & 0x7f), end='', flush=True)
        # 'V' (interrupt acknowledge), 'T' (time advance), 'Z' (reset): nothing to do here
        if op == ord('H') and ver >= 3: lines |= 0x08         # v3: target power is present
        out += bytes([res, lines])            # every request gets exactly one 2-byte reply
    conn.sendall(out)
