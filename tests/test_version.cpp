// test_version.cpp -- the version numbers in src/ffb_version.h agree with
// each other, and the version line names the product.
//
// FILEVERSION in the resource is built from the three numbers, FileVersion and
// the printed line from FFB_VERSION_STR. A bump that changes one and not the
// other shows two different versions for the same exe; this catches it.
#include "ffb_version.h"
#include "ffb_test.h"
#include <string>

int main() {
    const std::string from_numbers = std::to_string(FFB_VERSION_MAJOR) + "." +
                                     std::to_string(FFB_VERSION_MINOR) + "." +
                                     std::to_string(FFB_VERSION_PATCH);
    CHECK(from_numbers == FFB_VERSION_STR);
    CHECK(std::string(FFB_PRODUCT_NAME) == "FFB Co-op");
    CHECK(ffb_version_line() == std::string("FFB Co-op ") + FFB_VERSION_STR);

    // Release N is N.0.0 (#24, docs/SPEC.md "Versions"): the player sees "vN",
    // games.json carries "N.0.0", and nothing else is ever bumped.
    CHECK(FFB_VERSION_MAJOR >= 1);
    CHECK(FFB_VERSION_MINOR == 0);
    CHECK(FFB_VERSION_PATCH == 0);
    CHECK(ffb_window_title() == std::string("FFB Co-op v") + std::to_string(FFB_VERSION_MAJOR));
    return ffb_test_result();
}
