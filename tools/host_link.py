#!/usr/bin/env python3
"""Reference host computer for the 8550's REMOTE port (DOS/50 COMM).

The 8550 reaches it through tek8550 -remote (see notes/host-link.md):
  tek8550 -remote listen:8551 ...      host_link.py --connect 8551     (default)
  host_link.py --listen 8551           tek8550 -remote tcp:127.0.0.1:8551 ...

It behaves like a small time-sharing host: it echoes what is typed (COMM's
default E=R), prompts with "$ " (tell COMM: P=2420), and offers the transfer
side of the Tektronix general-host protocol (8540 System Users Manual,
"Intersystem Communication"):

  ls                       list the host directory (--dir, default .)
  type FILE                print a host file
  send FILE<NUL><CR>       formatted download: Standard Tekhex FILE -> 8550 memory.
                           FILE may be Tekhex, or a binary given as FILE@ADDR.
  recv FILE<NUL>lo hi [transfer]<CR>
                           formatted upload: 8550 memory -> Standard Tekhex FILE
  cat FILE<NUL>>dest<CR>   unformatted ASCII download: host text FILE -> 8550
                           file/device (CR line ends; ends with the prompt)
  save FILE<NUL><src<CR>   unformatted ASCII upload: 8550 file/device -> FILE;
                           when COMM shows RIOT:, end it with Ctrl-D <CR>
  bye                      close the line

(<NUL> is Ctrl-@ on the 8550 terminal: COMM keeps what follows for itself.)
Formatted transfers: block + prompt, answered "0" (ACK) or "7" (NAK) + CR.
"""
import argparse, os, socket, sys, time

ACK, NAK = b'0', b'7'


def nibsum(s):
    return sum(int(c, 16) for c in s) & 0xff


def tekhex_blocks(data, addr, transfer=None, per=30):
    """Standard Tekhex: /AAAACCSS<data>SS ... /TTTT00SS"""
    out = []
    for o in range(0, len(data), per):
        chunk = data[o:o + per]
        head = '%04X%02X' % ((addr + o) & 0xffff, len(chunk))
        body = ''.join('%02X' % b for b in chunk)
        out.append('/%s%02X%s%02X' % (head, nibsum(head), body, nibsum(body)))
    t = '%04X00' % ((addr if transfer is None else transfer) & 0xffff)
    out.append('/%s%02X' % (t, nibsum(t)))
    return out


def parse_block(b):
    """-> ('data', addr, bytes) | ('end', transfer, b'') | ('abort', msg, b'') | None if bad"""
    if b.startswith('//'):
        return ('abort', b[2:], b'')
    if not b.startswith('/') or len(b) < 9:
        return None
    try:
        head = b[1:7]
        addr, n = int(head[:4], 16), int(head[4:6], 16)
        if nibsum(head) != int(b[7:9], 16):
            return None
        if n == 0:
            return ('end', addr, b'')
        body = b[9:9 + 2 * n]
        if len(body) != 2 * n or nibsum(body) != int(b[9 + 2 * n:11 + 2 * n], 16):
            return None
        return ('data', addr, bytes.fromhex(body))
    except ValueError:
        return None


class Host:
    def __init__(self, sock, root, prompt, log, fill=8, echo=True):
        self.s, self.root, self.prompt, self.log, self.fill, self.echo = sock, root, prompt, log, fill, echo
        self.buf = b''

    # ---- line I/O
    def send(self, data):
        if isinstance(data, str):
            data = data.encode('latin-1')
        if self.log:
            print('[tx %r]' % data, file=sys.stderr)
        self.s.sendall(data)

    def getc(self, timeout=None):
        if not self.buf:
            self.s.settimeout(timeout)
            try:
                d = self.s.recv(4096)
            except socket.timeout:
                return None
            if not d:
                raise EOFError
            if self.log:
                print('[rx %r]' % d, file=sys.stderr)
            self.buf += d
        c, self.buf = self.buf[:1], self.buf[1:]
        return c

    def readline(self, echo=False, timeout=None):
        line = b''
        while True:
            c = self.getc(timeout)
            if c is None:
                return None
            if c in (b'\r', b'\n'):
                if echo:
                    self.send(b'\r\n')
                return line.decode('latin-1')
            if c in (b'\x08', b'\x7f'):
                if line:
                    line = line[:-1]
                    if echo:
                        self.send(b'\x08 \x08')
                continue
            line += c
            if echo and c != b'\x00':
                self.send(c)

    def path(self, name):
        p = os.path.normpath(os.path.join(self.root, name))
        if not p.startswith(os.path.normpath(self.root)):
            raise ValueError('outside the host directory')
        return p

    # ---- transfers
    def formatted_download(self, name):
        if '@' in name:
            f, a = name.rsplit('@', 1)
            blocks = tekhex_blocks(open(self.path(f), 'rb').read(), int(a, 16))
        else:
            blocks = [l.strip() for l in open(self.path(name), 'r', encoding='latin-1') if l.strip()]
        sent = 0
        for b in blocks:
            for tries in range(8):
                # Fill characters give COMM time to finish the previous block
                # (manual: "four to six fill characters" at 2400 baud). COMM reads
                # the block, waits for the prompt, then answers ACK/NAK + CR.
                self.send(' ' * self.fill + b + '\r' + self.prompt)
                # A host in COMM's default E=R mode echoes everything, the ACK line
                # included, and COMM V4.0 discards input up to that echo's CR
                # (routine 6330/6339) before reading the next block.
                reply = self.readline(echo=self.echo, timeout=30)
                if reply is None:
                    print('[send: no answer from the 8550]', file=sys.stderr)
                    return
                if reply.strip().startswith('0'):
                    break
                print('[send: NAK for %s]' % b[:12], file=sys.stderr)
            else:
                self.send('//TOO MANY NAKS\r' + self.prompt)
                return
            sent += 1
        print('[send: %d blocks]' % sent, file=sys.stderr)

    def formatted_upload(self, name):
        lines, data = [], 0
        self.send(self.prompt)
        while True:
            b = self.readline(echo=self.echo, timeout=60)
            if b is None:
                print('[recv: timed out]', file=sys.stderr)
                return
            b = b.strip()
            if not b:
                continue
            p = parse_block(b)
            if p is None:
                self.send(NAK + b'\r' + self.prompt.encode())
                continue
            lines.append(b)
            self.send(ACK + b'\r')
            if p[0] == 'data':
                data += len(p[2])
            if p[0] in ('end', 'abort'):
                break
            self.send(self.prompt)
        with open(self.path(name), 'w') as f:
            f.write('\n'.join(lines) + '\n')
        print('[recv: %d blocks, %d bytes -> %s]' % (len(lines), data, name), file=sys.stderr)

    def ascii_download(self, name):
        text = open(self.path(name), 'rb').read().replace(b'\r\n', b'\r').replace(b'\n', b'\r')
        self.send(text)
        if not text.endswith(b'\r'):
            self.send(b'\r')

    def ascii_upload(self, name):
        # COMM sends a line per prompt and shows "RIOT:" at the end of its file;
        # the user then types the host's end-of-file: Ctrl-D or Ctrl-Z alone on a
        # line here. Lines are written as they arrive.
        n = 0
        with open(self.path(name), 'w') as f:
            while True:
                self.send(self.prompt)
                line = self.readline(echo=self.echo)
                if line is None or line.strip() in ('\x04', '\x1a'):
                    break
                f.write(line + '\n'); f.flush()
                n += 1
        print('[save: %d lines -> %s]' % (n, name), file=sys.stderr)
        self.send('%d LINES SAVED\r\n' % n)

    # ---- the "shell"
    def run(self):
        self.send('\r\nTEKMDL REFERENCE HOST -- type "help"\r\n' + self.prompt)
        while True:
            line = self.readline(echo=self.echo)
            if line is None:
                continue
            cmd, _, directive = line.partition('\x00')     # COMM keeps what follows a NUL
            words = cmd.split()
            try:
                if not words:
                    pass
                elif words[0] == 'help':
                    self.send(__doc__.split('\n\n', 2)[2].replace('\n', '\r\n'))
                elif words[0] == 'ls':
                    self.send(''.join('%-24s %8d\r\n' % (n, os.path.getsize(self.path(n)))
                                      for n in sorted(os.listdir(self.root)) if os.path.isfile(self.path(n))))
                elif words[0] == 'type' and len(words) == 2:
                    self.send(open(self.path(words[1]), 'rb').read().replace(b'\n', b'\r\n'))
                elif words[0] == 'send' and len(words) == 2:
                    self.formatted_download(words[1])
                    continue
                elif words[0] == 'recv' and len(words) == 2:
                    self.formatted_upload(words[1])
                    continue
                elif words[0] == 'cat' and len(words) == 2:
                    self.ascii_download(words[1])
                elif words[0] == 'save' and len(words) == 2:
                    self.ascii_upload(words[1])
                elif words[0] in ('bye', 'logout', 'exit'):
                    self.send('BYE\r\n')
                    return
                else:
                    self.send('?\r\n')
            except (OSError, ValueError) as e:
                self.send('ERROR: %s\r\n' % e)
            self.send(self.prompt)


def main():
    ap = argparse.ArgumentParser(description='8550 REMOTE-port reference host (DOS/50 COMM)')
    g = ap.add_mutually_exclusive_group()
    g.add_argument('--connect', type=int, metavar='PORT', help='connect to tek8550 -remote listen:PORT (default 8551)')
    g.add_argument('--listen', type=int, metavar='PORT', help='wait for tek8550 -remote tcp:127.0.0.1:PORT')
    ap.add_argument('--dir', default='.', help='host directory for files (default .)')
    ap.add_argument('--prompt', default='$ ', help='host prompt (default "$ "; COMM P=2420)')
    ap.add_argument('--log', action='store_true', help='log received bytes')
    ap.add_argument('--no-echo', action='store_true', help="don't echo (for COMM E=L)")
    ap.add_argument('--fill', type=int, default=8, help='fill characters before each downloaded Tekhex block (default 8)')
    a = ap.parse_args()
    if a.listen:
        srv = socket.socket(); srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(('127.0.0.1', a.listen)); srv.listen(1)
        print('[host: waiting on 127.0.0.1:%d]' % a.listen, file=sys.stderr)
        s, _ = srv.accept()
    else:
        port = a.connect or 8551
        for _ in range(300):
            try:
                s = socket.create_connection(('127.0.0.1', port)); break
            except OSError:
                time.sleep(0.2)
        else:
            sys.exit('host: cannot connect to 127.0.0.1:%d' % port)
    print('[host: line up]', file=sys.stderr)
    try:
        Host(s, os.path.abspath(a.dir), a.prompt, a.log, a.fill, not a.no_echo).run()
    except EOFError:
        pass
    print('[host: line down]', file=sys.stderr)


if __name__ == '__main__':
    main()
