// ffb_version.h -- the one place FFB Co-op.exe's version lives.
//
// Included by res/ffb_coop.rc (the version resource Explorer shows) and by the
// C++ code (the version line the exe prints), so the two cannot drift. Keep the
// part above RC_INVOKED to plain #defines: rc.exe only understands those.
#pragma once

// Release N is N.0.0 (docs/SPEC.md, "Versions"): v1 is 1.0.0, v2 is 2.0.0.
// MINOR and PATCH stay 0; tests/test_version.cpp checks it.
#define FFB_VERSION_MAJOR 2
#define FFB_VERSION_MINOR 0
#define FFB_VERSION_PATCH 0
#define FFB_VERSION_STR   "2.0.0"

#define FFB_PRODUCT_NAME  "FFB Co-op"

#ifndef RC_INVOKED
#include <string>

// "FFB Co-op 2.0.0" -- the first line the exe prints.
inline std::string ffb_version_line() {
    return std::string(FFB_PRODUCT_NAME) + " " + FFB_VERSION_STR;
}

// "FFB Co-op v2" -- the console window's title, the name players and the site use.
inline std::string ffb_window_title() {
    return std::string(FFB_PRODUCT_NAME) + " v" + std::to_string(FFB_VERSION_MAJOR);
}
#endif
