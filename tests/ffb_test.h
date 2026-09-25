// ffb_test.h -- the whole unit-test framework: CHECK prints ok/FAIL per line,
// and a test's main returns ffb_test_result(), non-zero when anything failed,
// which is what ctest reads.
#pragma once
#include <cstdio>

static int g_ffb_fail = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (cond) std::printf("  ok    %s\n", #cond);                   \
        else { std::printf("  FAIL  %s  (%s:%d)\n", #cond, __FILE__, __LINE__); ++g_ffb_fail; } \
    } while (0)

static int ffb_test_result() {
    if (g_ffb_fail) { std::printf("%d FAILED\n", g_ffb_fail); return 1; }
    std::printf("all passed\n");
    return 0;
}
