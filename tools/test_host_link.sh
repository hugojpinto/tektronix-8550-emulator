#!/bin/bash
# Host-link regression: DOS/50 COMM against tools/host_link.py over the 8301 REMOTE port.
# Formatted download and upload (Tekhex), ASCII download into a file, ASCII upload of a file.
# usage: tools/test_host_link.sh   (needs emu/build/tek8550 and python3; ~10 s)
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
PORT=$((18700 + RANDOM % 200))
printf '\x3e\x41\xd3\x10\x76\x00\x11\x22\x33\x44\x55\x66\x77\x88\x99\xaa' > "$W/prog.bin"
printf 'FIRST LINE FROM THE HOST\nSECOND LINE\n' > "$W/notes.txt"
cp "$ROOT/disks/System_helpfix.img" "$W/sys.img"
python3 -I "$ROOT/tools/host_link.py" --connect $PORT --dir "$W" > "$W/host.log" 2>&1 &
HPID=$!
types=('SEL Z80\r' 'COMM P=2420\r' 'send prog.bin@100\x00\r' 'recv up.hex\x00100 10F\r'
       'cat notes.txt\x00>NOTES\r' 'save kong.txt\x00<MAKE.KONG\r' '\x04\r' '\x00\e' 'D 100 10F\r' 'COPY NOTES CONO\r')
a=(); t=32; for c in "${types[@]}"; do a+=(-type $t "$c"); t=$((t+8)); done
(cd "$ROOT/emu" && ./build/tek8550 -rw -term tty -remote listen:$PORT -0 "$W/sys.img" -cycles $(( (t+10)*2000000 )) "${a[@]}" </dev/null 2>/dev/null) | tr '\r' '\n' > "$W/emu.log"
wait $HPID 2>/dev/null
pass=0; fail=0
check() { if eval "$2"; then pass=$((pass+1)); else fail=$((fail+1)); echo "FAIL: $1"; fi; }
check "COMM starts"               "grep -q 'COMM V4.0' '$W/emu.log'"
check "download: block received"  "grep -q 'BLOCK RECEIVED AT: 0100 10' '$W/emu.log'"
check "download: memory"          "grep -q '0100 3E 41 D3 10 76 00 11 22 33 44 55 66 77 88 99 AA' '$W/emu.log'"
check "upload: Tekhex file"       "grep -q '^/010010023E41D3107600112233445566778899AAA2' '$W/up.hex' 2>/dev/null"
check "upload: termination"       "grep -qE '^/[0-9A-F]{4}00[0-9A-F]{2}$' '$W/up.hex' 2>/dev/null"
check "ASCII download to a file"  "grep -q 'SECOND LINE' '$W/emu.log'"
check "ASCII upload of a file"    "grep -q 'DUMP 0 3FFF KONG.DUMP' '$W/kong.txt' 2>/dev/null"
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
