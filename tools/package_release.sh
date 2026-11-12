#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
# Package a binary release: the programs with the ROMs, discs and front-panel artwork beside them
# (the programs look for roms/ and artwork/ next to themselves), as a .tar.gz, or a .zip for
# Windows. Used by .github/workflows/release.yml; works locally too.
# usage: tools/package_release.sh PLATFORM BUILD_DIR OUT_DIR    (e.g. macos-universal emu/build dist)
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NAME=tektronix-8550-emulator
BINS=(tek8550 tek8550-gui tek8301 tek8501 dasm)
PLATFORM="$1"; BUILD="$2"; OUT="$3"
[ -n "$PLATFORM" ] && [ -d "$BUILD" ] && [ -n "$OUT" ] || { echo "usage: $0 PLATFORM BUILD_DIR OUT_DIR"; exit 1; }
VERSION="$(cat "$ROOT/VERSION")"
EXE=; case "$PLATFORM" in windows*) EXE=.exe;; esac
D="$OUT/$NAME-$VERSION-$PLATFORM"
rm -rf "$D"; mkdir -p "$D"
for b in "${BINS[@]}"; do cp "$BUILD/$b$EXE" "$D/"; done
[ -f "$BUILD/SDL3.dll" ] && cp "$BUILD/SDL3.dll" "$D/"
cp -R "$ROOT/roms" "$ROOT/disks" "$D/"
cp -R "$ROOT/emu/artwork" "$D/artwork"
cp "$ROOT/README.md" "$ROOT/LICENSE" "$ROOT/NOTICE.md" "$ROOT/CHANGELOG.md" "$ROOT/VERSION" "$D/"
X=./; [ -n "$EXE" ] && X=
cat > "$D/QUICKSTART.txt" <<TXT
Tektronix 8550 Microcomputer Development Lab emulator $VERSION ($PLATFORM)

  ${X}tek8550$EXE -0 disks/System_Fixed.img        the lab on this terminal: DOS/50's prompt after
                                                about 27 s; Ctrl-] opens the monitor, q quits
  ${X}tek8550-gui$EXE -0 disks/System_Fixed.img    the CT8500 window and the 8301/8501 front panels

Keep roms/ and artwork/ beside the programs. -rw writes changes back to a disc image: use it on a
copy. ${X}tek8550$EXE -version prints the version. Options, tutorials and the hardware notes:
https://github.com/hugojpinto/tektronix-8550-emulator
TXT
case "$PLATFORM" in
macos*) printf '\nmacOS: the programs are not signed or notarised, so Gatekeeper blocks them at first. Clear\nthe download quarantine once, in Terminal, in this folder: xattr -dr com.apple.quarantine .\n(or run a program, then System Settings > Privacy & Security > Open Anyway, for each one).\n' >> "$D/QUICKSTART.txt";;
windows*) printf '\r\nWindows: the programs are not code-signed, so SmartScreen may warn: More info > Run anyway.\r\nUnblocking the zip before extracting avoids it (Properties > Unblock, or Unblock-File in PowerShell).\r\n' >> "$D/QUICKSTART.txt";;
esac
cd "$OUT"
if [ -n "$EXE" ]; then rm -f "$NAME-$VERSION-$PLATFORM.zip"; zip -qr "$NAME-$VERSION-$PLATFORM.zip" "$NAME-$VERSION-$PLATFORM"; echo "$OUT/$NAME-$VERSION-$PLATFORM.zip"
else tar czf "$NAME-$VERSION-$PLATFORM.tar.gz" "$NAME-$VERSION-$PLATFORM"; echo "$OUT/$NAME-$VERSION-$PLATFORM.tar.gz"; fi
