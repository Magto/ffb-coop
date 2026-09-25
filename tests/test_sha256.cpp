// test_sha256.cpp -- src/net.cpp's sha256 against the FIPS 180-2 example
// vectors (appendix B.1-B.3) and the empty string, and the hex comparisons the
// package update uses.
#include "ffb_test.h"
#include "net.h"

#include <string>

int main() {
    CHECK(ffb::sha256_hex(std::string()) ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(ffb::sha256_hex(std::string("abc")) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const std::string two_block = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    CHECK(ffb::sha256_hex(two_block) ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    // One million 'a', fed in uneven pieces so a piece crosses every block edge.
    ffb::Sha256 s;
    const std::string chunk(997, 'a');
    std::size_t left = 1000000;
    while (left) {
        const std::size_t n = left < chunk.size() ? left : chunk.size();
        s.feed(chunk.data(), n);
        left -= n;
    }
    CHECK(s.finish_hex() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    // Byte-at-a-time gives the same hash as one feed.
    ffb::Sha256 bytes;
    for (char c : two_block) bytes.feed(&c, 1);
    CHECK(bytes.finish_hex() == ffb::sha256_hex(two_block));

    const std::string lower = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    const std::string upper = "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD";
    CHECK(ffb::is_sha256_hex(lower));
    CHECK(ffb::is_sha256_hex(upper));
    CHECK(!ffb::is_sha256_hex(lower.substr(1)));
    CHECK(!ffb::is_sha256_hex(lower + "0"));
    CHECK(!ffb::is_sha256_hex("g" + lower.substr(1)));
    CHECK(ffb::same_sha256(lower, upper));
    CHECK(!ffb::same_sha256(lower, "ca" + lower.substr(2)));
    return ffb_test_result();
}
