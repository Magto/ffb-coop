// main.cpp -- FFB Co-op.exe entry point.
//
// Scaffold only (#2): prints the version line and exits 0. The launcher
// behaviour (self-update, games.json, package download, start) lands in #3-#6.
#include "ffb_version.h"
#include <cstdio>

int main() {
    std::printf("%s\n", ffb_version_line().c_str());
    return 0;
}
