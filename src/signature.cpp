// signature.cpp -- see signature.h.
#include "signature.h"

#include <algorithm>
#include <cstdlib>

#include "trusted_keys.h"

extern "C" {
#include "tweetnacl.h"

// TweetNaCl's key and box generators draw from randombytes(). FFB Co-op.exe
// only verifies -- it never makes a key -- so nothing here calls them, and a
// call that ever did would be a bug: stop rather than make a key from nothing.
void randombytes(unsigned char*, unsigned long long) { std::abort(); }
}

namespace ffb {

namespace {

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Exactly 2*n hex digits -> n bytes.
bool parse_hex(const std::string& hex, unsigned char* out, std::size_t n) {
    if (hex.size() != 2 * n) return false;
    for (std::size_t i = 0; i < n; ++i) {
        const int hi = hex_digit(hex[2 * i]), lo = hex_digit(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (unsigned char)(hi << 4 | lo);
    }
    return true;
}

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

}  // namespace

const std::vector<PublicKey>& trusted_keys() {
    static const std::vector<PublicKey> keys = [] {
        std::vector<PublicKey> v;
        for (const char* hex : kTrustedKeyHex) {
            PublicKey k;
            // A malformed entry is a build-time typo; it trusts nothing rather than
            // something else.
            if (parse_public_key(hex, &k)) v.push_back(k);
        }
        return v;
    }();
    return keys;
}

std::string signature_url(const std::string& url) {
    return url.substr(0, url.find_first_of("?#")) + ".sig";
}

bool parse_public_key(const std::string& hex, PublicKey* out) {
    return parse_hex(hex, out->data(), out->size());
}

bool parse_signature(const std::string& text, Signature* out, std::string* why) {
    std::size_t end = text.size();
    while (end > 0 && is_space(text[end - 1])) --end;
    if (!parse_hex(text.substr(0, end), out->data(), out->size())) {
        if (why) *why = "the signature is not 128 hex digits";
        return false;
    }
    return true;
}

bool ed25519_verify(const Signature& sig, const std::string& data, const PublicKey& key) {
    // TweetNaCl checks a signed message: the signature followed by the data. It
    // uses `m` as scratch, so it is as long as the signed message.
    std::vector<unsigned char> sm(sig.size() + data.size()), m(sm.size());
    std::copy(sig.begin(), sig.end(), sm.begin());
    std::copy(data.begin(), data.end(), sm.begin() + sig.size());
    unsigned long long mlen = 0;
    return crypto_sign_open(m.data(), &mlen, sm.data(), sm.size(), key.data()) == 0 &&
           mlen == data.size();
}

std::string check_signature(const std::string& what, const std::string& data,
                            const std::string& sig_text, const std::vector<PublicKey>& keys) {
    Signature sig;
    std::string why;
    if (!parse_signature(sig_text, &sig, &why)) return what + ": " + why;
    for (const auto& k : keys)
        if (ed25519_verify(sig, data, k)) return std::string();
    return what + ": the signature does not match";
}

}  // namespace ffb
