// ffb_version.h -- the one place FFB Co-op.exe's version lives.
//
// Included by res/ffb_coop.rc (the version resource Explorer shows) and by the
// C++ code (the version line the exe prints), so the two cannot drift. Keep the
// part above RC_INVOKED to plain #defines: rc.exe only understands those.
#pragma once

// Release N is N.0.0 (docs/SPEC.md, "Versions"): v1 is 1.0.0, v2 is 2.0.0.
// MINOR and the public PATCH stay 0; tests/test_version.cpp checks it.
#define FFB_VERSION_MAJOR 1
#define FFB_VERSION_MINOR 0

// The dev channel (#26, docs/SPEC.md "Dev channel"): FFB Co-op - dev.exe is
// MAJOR.0.D, dev build D of the release in the works. Bump FFB_DEV_BUILD and
// FFB_DEV_VERSION_STR together before every `tools/publish.py --dev`: a dev exe
// only updates to a higher version, exactly like the public one.
#define FFB_DEV_BUILD       1
#define FFB_DEV_VERSION_STR "1.0.1"

#ifdef FFB_DEV_CHANNEL
#define FFB_VERSION_PATCH FFB_DEV_BUILD
#define FFB_VERSION_STR   FFB_DEV_VERSION_STR
#define FFB_PRODUCT_NAME  "FFB Co-op - dev"
#define FFB_EXE_NAME      "FFB Co-op - dev.exe"
#else
#define FFB_VERSION_PATCH 0
#define FFB_VERSION_STR   "1.0.0"
#define FFB_PRODUCT_NAME  "FFB Co-op"
#define FFB_EXE_NAME      "FFB Co-op.exe"
#endif

#ifndef RC_INVOKED
#include <string>

// "FFB Co-op 1.0.0" -- the first line the exe prints ("FFB Co-op - dev 1.0.1"
// for the dev exe).
inline std::string ffb_version_line() {
    return std::string(FFB_PRODUCT_NAME) + " " + FFB_VERSION_STR;
}

// "FFB Co-op v1" -- the console window's title, the name players and the site use
// ("FFB Co-op - dev v1" for the dev exe).
inline std::string ffb_window_title() {
    return std::string(FFB_PRODUCT_NAME) + " v" + std::to_string(FFB_VERSION_MAJOR);
}
#endif
