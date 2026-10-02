// test_version_dev.cpp -- src/ffb_version.h as the dev exe sees it (#26),
// built with FFB_DEV_CHANNEL like the ffb_coop_dev target: its numbers agree
// with its string, it is MAJOR.0.D with D at least 1, and the names say dev.
// tests/test_version.cpp checks the public side.
#include "ffb_version.h"
#include "channel.h"
#include "ffb_test.h"
#include <string>

int main() {
    const std::string from_numbers = std::to_string(FFB_VERSION_MAJOR) + "." +
                                     std::to_string(FFB_VERSION_MINOR) + "." +
                                     std::to_string(FFB_VERSION_PATCH);
    CHECK(from_numbers == FFB_VERSION_STR);
    CHECK(std::string(FFB_DEV_VERSION_STR) == FFB_VERSION_STR);
    CHECK(FFB_VERSION_PATCH == FFB_DEV_BUILD);

    // A dev build is MAJOR.0.D, D >= 1: never N.0.0, so tools/publish.py can tell
    // a dev exe from a release exe by the version resource alone.
    CHECK(FFB_VERSION_MAJOR >= 1);
    CHECK(FFB_VERSION_MINOR == 0);
    CHECK(FFB_DEV_BUILD >= 1);

    CHECK(std::string(FFB_PRODUCT_NAME) == "FFB Co-op - dev");
    CHECK(std::string(FFB_EXE_NAME) == "FFB Co-op - dev.exe");
    CHECK(ffb_version_line() == std::string("FFB Co-op - dev ") + FFB_VERSION_STR);
    CHECK(ffb_window_title() == std::string("FFB Co-op - dev v") + std::to_string(FFB_VERSION_MAJOR));

    // The build switch picks the dev channel, and the exe name agrees with it.
    CHECK(&ffb::this_channel() == &ffb::dev_channel());
    CHECK(std::string(ffb::this_channel().exe_name) == FFB_EXE_NAME);
    return ffb_test_result();
}
