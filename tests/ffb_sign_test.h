// ffb_sign_test.h -- signs test fixtures, so a test can serve a games.json or a
// manifest with a valid .sig beside it (#21).
//
// The keys are RFC 8032's own test keys (section 7.1, TEST 1 and TEST 2), never
// the real coopmods key: the private half of that is in no repo. Signing goes
// through TweetNaCl's crypto_sign, which test_signature checks against the
// RFC's own signatures.
#pragma once

#include <string>
#include <vector>

#include "signature.h"

extern "C" {
#include "tweetnacl.h"
}

namespace ffb_test {

// RFC 8032 7.1 TEST 1: the key every test launcher trusts.
const char* const kTestSeedHex   = "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60";
const char* const kTestPublicHex = "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a";
// RFC 8032 7.1 TEST 2: a well-formed key nobody trusts -- the "wrong key".
const char* const kOtherSeedHex   = "4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb";
const char* const kOtherPublicHex = "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c";

inline std::string to_bytes(const std::string& hex) {
    std::string out;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) out += (char)std::stoi(hex.substr(i, 2), nullptr, 16);
    return out;
}

inline std::string to_hex(const unsigned char* p, std::size_t n) {
    static const char d[] = "0123456789abcdef";
    std::string s;
    for (std::size_t i = 0; i < n; ++i) { s += d[p[i] >> 4]; s += d[p[i] & 15]; }
    return s;
}

inline ffb::PublicKey key(const char* hex) {
    ffb::PublicKey k{};
    ffb::parse_public_key(hex, &k);
    return k;
}

// The keys a test launcher trusts: TEST 1 only.
inline const std::vector<ffb::PublicKey>& test_keys() {
    static const std::vector<ffb::PublicKey> keys{key(kTestPublicHex)};
    return keys;
}

// The text of a .sig file for `data`, as tools/signing.py writes it: 128
// lowercase hex digits and a newline. TweetNaCl's secret key is seed || public.
inline std::string sign(const std::string& data, const char* seed_hex = kTestSeedHex,
                        const char* public_hex = kTestPublicHex) {
    const std::string sk = to_bytes(seed_hex) + to_bytes(public_hex);
    std::vector<unsigned char> sm(data.size() + 64);
    unsigned long long smlen = 0;
    crypto_sign(sm.data(), &smlen, (const unsigned char*)data.data(), data.size(),
                (const unsigned char*)sk.data());
    return to_hex(sm.data(), 64) + "\n";
}

}  // namespace ffb_test
