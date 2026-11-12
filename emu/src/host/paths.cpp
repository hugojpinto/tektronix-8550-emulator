// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// Version and data paths -- see paths.h.
#include "host/paths.h"
#include <cstdio>
#include <sys/stat.h>
#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <climits>
#include <cstdlib>
#elif defined(__linux__)
#include <unistd.h>
#include <climits>
#endif

#ifndef TEK_VERSION
#define TEK_VERSION "unknown"
#endif

const char *host::version() { return TEK_VERSION; }

bool host::version_requested(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		const std::string a = argv[i];
		if (a != "-version" && a != "--version") continue;
		std::string name = argv[0];
		const size_t s = name.find_last_of("/\\");
		if (s != std::string::npos) name = name.substr(s + 1);
		if (name.size() > 4 && name.compare(name.size() - 4, 4, ".exe") == 0) name.resize(name.size() - 4);
		printf("%s %s\n", name.c_str(), version());
		return true;
	}
	return false;
}

std::string host::exe_dir()
{
	std::string p;
#if defined(_WIN32)
	char buf[MAX_PATH];
	const DWORD n = GetModuleFileNameA(nullptr, buf, sizeof buf);
	if (n > 0 && n < sizeof buf) p.assign(buf, n);
#elif defined(__APPLE__)
	char buf[PATH_MAX], real[PATH_MAX];
	uint32_t size = sizeof buf;
	if (_NSGetExecutablePath(buf, &size) == 0) p = realpath(buf, real) ? real : buf;
#elif defined(__linux__)
	char buf[PATH_MAX];
	const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
	if (n > 0) p.assign(buf, size_t(n));
#endif
	const size_t s = p.find_last_of("/\\");
	return s == std::string::npos ? std::string() : p.substr(0, s);
}

std::string host::data_path(const std::string &path)
{
	struct stat st;
	auto exists = [&](const std::string &f) { return stat(f.c_str(), &st) == 0; };
	if (path.empty() || exists(path) || path[0] == '/' || path[0] == '\\' || (path.size() > 1 && path[1] == ':')) return path;
	std::string rel = path;
	while (rel.compare(0, 3, "../") == 0 || rel.compare(0, 2, "./") == 0) rel.erase(0, rel[0] == '.' && rel[1] == '/' ? 2 : 3);
	const std::string d = exe_dir();
	if (d.empty()) return path;
	for (const std::string &c : { d + "/" + rel, d + "/../" + rel, d + "/../../" + rel })   // a release; bin/; emu/build/
		if (exists(c)) return c;
	return path;
}
