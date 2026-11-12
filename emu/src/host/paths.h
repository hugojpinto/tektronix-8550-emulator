// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// The program's version, and where its data (ROMs, artwork) is: in the repository the programs
// run from emu/ with ../roms; an unpacked release keeps roms/ and artwork/ beside the programs.
#ifndef TEK_HOST_PATHS_H
#define TEK_HOST_PATHS_H
#include <string>

namespace host {
const char *version();                              // the repository's VERSION file, compiled in
// -version or --version anywhere on the command line: prints "NAME VERSION" and returns true
bool version_requested(int argc, char **argv);
std::string exe_dir();                              // the running program's directory ("" if unknown)
// A default data path: as given, if it exists (relative to the current directory); otherwise the
// same path without its leading "../", beside the program, or one or two levels above it (a
// repository build in emu/build); otherwise as given (the caller reports it missing).
std::string data_path(const std::string &path);
}
#endif
