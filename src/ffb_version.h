// ffb_version.h -- the one place FFB Co-op.exe's version lives.
//
// Included by res/ffb_coop.rc (the version resource Explorer shows) and by the
// C++ code (the version line the exe prints), so the two cannot drift. Keep the
// part above RC_INVOKED to plain #defines: rc.exe only understands those.
#pragma once

#define FFB_VERSION_MAJOR 0
#define FFB_VERSION_MINOR 1
#define FFB_VERSION_PATCH 0
#define FFB_VERSION_STR   "0.1.0"

#define FFB_PRODUCT_NAME  "FFB Co-op"

#ifndef RC_INVOKED
#include <string>

// "FFB Co-op 0.1.0" -- the first line the exe prints.
inline std::string ffb_version_line() {
    return std::string(FFB_PRODUCT_NAME) + " " + FFB_VERSION_STR;
}
#endif
