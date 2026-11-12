#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
"""Reference prototype (target system) for the 8550 and 8002 emulators' Z80 probe (also the
8002's 6800 probe, which uses R/W only).

Protocol v3 (see notes/prototype-link.md; v1/v2 clients work unchanged): the emulator connects and sends
4-byte requests  [op, addr_lo, addr_hi, data];  each gets a 2-byte reply
[data, lines] where lines bit0 = INT asserted, bit1 = NMI asserted;
v3: bit2 = RESET asserted, bits 7-4 = WAIT T-states for this cycle.
  H hello (addr = client version) -> data = min(client, 3); v3: lines bit3 = power present
  R/W memory read/write   F opcode fetch (M1 read, v2)
  I/O io read/write (port = addr_lo; addr_hi = A8-A15, ignored here)
  V interrupt acknowledge -> data bus value (vector / RST opcode)
  T time advance, addr = microseconds      Z reset
  K (v4) the clock the probe takes from the prototype: addr 0 -> kHz low byte, addr 1 -> high

This target: 64 KB RAM, optional ROM images, a console UART
  port 10h  data (write: output to this terminal; read: next input char)
  port 11h  status  bit0 = input ready, bit1 = output ready (always 1)
and an optional periodic interrupt (--tick MS) answering INTA with RST 38h (FFh),
cleared by writing any value to port 12h. v3 test aids: --io-wait N inserts N WAIT
states in every I/O cycle (a slow peripheral); --reset-at MS pulses RESET once.

usage: proto_target.py [--unix PATH | --tcp PORT] [--rom FILE@HEXADDR ...] [--tick MS]
"""
import argparse, os, select, socket, sys

def main():
    ap = argparse.ArgumentParser(description='8550/8002 prototype target (IPC)')
    ap.add_argument('--unix', help='listen on a Unix socket path')
    ap.add_argument('--tcp', type=int, help='listen on TCP port (localhost)')
    ap.add_argument('--rom', action='append', default=[], help='FILE@HEXADDR to preload')
    ap.add_argument('--tick', type=float, default=0, help='periodic INT every MS milliseconds')
    ap.add_argument('--log', action='store_true', help='log every access to stderr')
    ap.add_argument('--io-wait', type=int, default=0, help='v3: WAIT states in every I/O cycle (0-15)')
    ap.add_argument('--reset-at', type=float, default=0, help='v3: pulse RESET once at MS milliseconds')
    ap.add_argument('--clock', type=float, default=0, help='v4: the prototype clock the probe feeds the emulator processor, in MHz (0 = not reported)')
    args = ap.parse_args()

    ram = bytearray(b'\xff' * 0x10000)
    for spec in args.rom:
        path, _, addr = spec.rpartition('@')
        data = open(path, 'rb').read()
        a = int(addr, 16)
        ram[a:a + len(data)] = data
    inq = bytearray()
    us = 0.0
    tick_us = args.tick * 1000.0
    next_tick = tick_us if tick_us else None
    irq = False
    ver = 1
    reset_at = args.reset_at * 1000.0 if args.reset_at else None
    rst = False

    if args.unix:
        if os.path.exists(args.unix): os.unlink(args.unix)
        srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM); srv.bind(args.unix)
        where = args.unix
    else:
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(('127.0.0.1', args.tcp or 8550)); where = 'tcp port %d' % (args.tcp or 8550)
    srv.listen(1)
    print('[prototype target listening on %s]' % where, file=sys.stderr, flush=True)
    conn, _ = srv.accept()
    print('[emulator connected]', file=sys.stderr, flush=True)
    stdin_ok = sys.stdin.isatty() or not sys.stdin.closed
    buf = b''
    while True:
        # gather console input without blocking
        if stdin_ok:
            r, _, _ = select.select([sys.stdin], [], [], 0)
            if r:
                ch = os.read(sys.stdin.fileno(), 64)
                if not ch: stdin_ok = False
                else: inq += ch.replace(b'\n', b'\r')
        data = conn.recv(4096)
        if not data: break
        buf += data
        out = bytearray()
        while len(buf) >= 4:
            op, lo, hi, d = buf[0], buf[1], buf[2], buf[3]; buf = buf[4:]
            a = lo | (hi << 8); res = 0xff
            wait = 0
            if op == ord('H'): res = ver = max(1, min(a, 4))
            elif op == ord('K'):                              # v4: the clock, in kHz, low byte (addr 0) then high (addr 1)
                khz = int(round(args.clock * 1000))
                res = (khz >> 8 if a & 1 else khz) & 0xff
            elif op in (ord('R'), ord('F')): res = ram[a]
            elif op == ord('W'): ram[a] = d
            elif op == ord('I'):
                wait = args.io_wait
                if lo == 0x10: res = inq.pop(0) if inq else 0
                elif lo == 0x11: res = (1 if inq else 0) | 2
            elif op == ord('O'):
                wait = args.io_wait
                if lo == 0x10: sys.stdout.write(chr(d & 0x7f)); sys.stdout.flush()
                elif lo == 0x12: irq = False
            elif op == ord('V'): res = 0xff                    # RST 38h
            elif op == ord('T'):
                us += a
                if next_tick is not None and us >= next_tick:
                    irq = True; next_tick += tick_us
                if reset_at is not None and us >= reset_at:
                    rst = True; reset_at = None
                    print('[target RESET]', file=sys.stderr, flush=True)
                elif rst:
                    rst = False                                # a pulse: one T period long
            elif op == ord('Z'): irq = False
            if args.log and op not in (ord('T'),):
                print('[%s %04x %02x -> %02x]' % (chr(op), a, d, res), file=sys.stderr)
            lines = 1 if irq else 0
            if ver >= 3:
                if op == ord('H'): lines |= 8                 # target power present
                else: lines |= (4 if rst else 0) | (min(wait, 15) << 4)
            out += bytes([res, lines])
        conn.sendall(out)
    print('[emulator disconnected after %.0f us of target time]' % us, file=sys.stderr)

if __name__ == '__main__':
    main()
