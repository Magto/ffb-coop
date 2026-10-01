// test_signature.cpp -- the ed25519 check on games.json and on a manifest (#21,
// docs/SPEC.md "Signatures"): the RFC 8032 vectors, the .sig format, the
// compiled-in key, and the valid / missing / wrong-key / tampered-byte cases on
// both kinds of file.
//
// Expected signatures are RFC 8032's (section 7.1), not output of this code.
// What happens to a refused file in the flow -- no self-update, no file changed
// -- is test_app's and test_package's.
#include "ffb_test.h"
#include "ffb_sign_test.h"
#include "signature.h"

#include <string>

using namespace ffb_test;

namespace {

// RFC 8032 7.1: TEST 1 signs the empty message, TEST 2 the one byte 0x72.
const char* const kRfcSig1 =
    "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b";
const char* const kRfcSig2 =
    "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00";

// The key generated for coopmods on 2026-10-01, as docs/SPEC.md "Signatures" names it.
const char* const kCoopmodsKey = "43706304f0fae090f7a2afd96e06d207ab193ea52a527d6576073ce281b30a59";

ffb::Signature sig_of(const std::string& hex) {
    ffb::Signature s{};
    ffb::parse_signature(hex, &s, nullptr);
    return s;
}

const std::string kGamesJson =
    "{\"schema\":1,\"launcher\":{\"version\":\"1.0.0\",\"url\":\"https://coopmods.com/launcher/FFB%20Co-op.exe\","
    "\"sha256\":\"" + std::string(64, 'a') + "\",\"size\":412160},\"games\":[{\"id\":\"mewgenics\",\"name\":"
    "\"Mewgenics\",\"exe\":\"Mewgenics.exe\",\"steam_appid\":0,\"manifest\":"
    "\"https://mewgenics.coopmods.com/update/manifest.json\",\"launcher\":\"mewcoop_loader.exe\"}]}\n";
const std::string kManifest =
    "{\"version\":\"76.0.0\",\"wire\":34,\"files\":[{\"name\":\"mewcoop.dll\",\"size\":4194304,\"sha256\":\"" +
    std::string(64, 'b') + "\",\"required\":true}]}\n";

// The four cases the issue names, on one file.
void four_cases(const char* what, const std::string& file) {
    std::printf("%s: valid, missing, wrong key, tampered byte\n", what);
    const auto& keys = test_keys();
    const std::string good = sign(file);

    CHECK(ffb::check_signature(what, file, good, keys).empty());                        // valid
    CHECK(!ffb::check_signature(what, file, "", keys).empty());                         // missing (empty .sig)
    CHECK(ffb::check_signature(what, file, "", keys).find("not 128 hex digits") != std::string::npos);
    const std::string other = sign(file, kOtherSeedHex, kOtherPublicHex);
    CHECK(ffb::check_signature(what, file, other, keys) ==
          std::string(what) + ": the signature does not match");                     // wrong key

    // One byte changed, anywhere in the file: first, middle, last. Same length.
    for (std::size_t at : {std::size_t(0), file.size() / 2, file.size() - 1}) {
        std::string t = file;
        t[at] = (char)(t[at] ^ 0x01);
        CHECK(ffb::check_signature(what, t, good, keys) == std::string(what) + ": the signature does not match");
    }
    // A byte added or removed at the end (a trailing newline is part of the file).
    CHECK(!ffb::check_signature(what, file + " ", good, keys).empty());
    CHECK(!ffb::check_signature(what, file.substr(0, file.size() - 1), good, keys).empty());
    // A byte of the signature itself changed.
    std::string bad_sig = good;
    bad_sig[10] = bad_sig[10] == '0' ? '1' : '0';
    CHECK(!ffb::check_signature(what, file, bad_sig, keys).empty());
    // No trusted key at all: nothing passes.
    CHECK(!ffb::check_signature(what, file, good, {}).empty());
    // The wrong-key signature passes once its key is trusted: a second key in
    // the list is how a rotation works (docs/SPEC.md).
    const std::vector<ffb::PublicKey> both{key(kTestPublicHex), key(kOtherPublicHex)};
    CHECK(ffb::check_signature(what, file, other, both).empty());
    CHECK(ffb::check_signature(what, file, good, both).empty());
}

}  // namespace

int main() {
    std::puts("RFC 8032 7.1 TEST 1 and TEST 2 verify; a changed message or signature does not");
    {
        CHECK(ffb::ed25519_verify(sig_of(kRfcSig1), "", key(kTestPublicHex)));
        CHECK(ffb::ed25519_verify(sig_of(kRfcSig2), "\x72", key(kOtherPublicHex)));
        CHECK(!ffb::ed25519_verify(sig_of(kRfcSig2), "\x73", key(kOtherPublicHex)));
        CHECK(!ffb::ed25519_verify(sig_of(kRfcSig1), "", key(kOtherPublicHex)));
        CHECK(!ffb::ed25519_verify(sig_of(kRfcSig2), "", key(kTestPublicHex)));
        // The fixture signer reproduces the RFC's signatures exactly.
        CHECK(sign("") == std::string(kRfcSig1) + "\n");
        CHECK(sign("\x72", kOtherSeedHex, kOtherPublicHex) == std::string(kRfcSig2) + "\n");
    }

    std::puts(".sig format: 128 hex digits, either case, optional trailing whitespace");
    {
        ffb::Signature s{};
        std::string why;
        CHECK(ffb::parse_signature(kRfcSig1, &s, &why));
        CHECK(to_hex(s.data(), s.size()) == kRfcSig1);
        std::string upper = kRfcSig1;
        for (char& c : upper) if (c >= 'a' && c <= 'f') c = (char)(c - 'a' + 'A');
        CHECK(ffb::parse_signature(upper, &s, &why) && to_hex(s.data(), s.size()) == kRfcSig1);
        CHECK(ffb::parse_signature(std::string(kRfcSig1) + "\r\n", &s, &why));
        CHECK(ffb::parse_signature(std::string(kRfcSig1) + "\n", &s, &why));
        CHECK(!ffb::parse_signature("", &s, &why));
        CHECK(why == "the signature is not 128 hex digits");
        CHECK(!ffb::parse_signature(std::string(kRfcSig1).substr(1), &s, &why));   // 127 digits
        CHECK(!ffb::parse_signature(std::string(kRfcSig1) + "0", &s, &why));        // 129
        CHECK(!ffb::parse_signature(" " + std::string(kRfcSig1), &s, &why));        // leading space
        CHECK(!ffb::parse_signature("g" + std::string(kRfcSig1).substr(1), &s, &why));
        CHECK(!ffb::parse_signature(std::string(kRfcSig1) + "\n" + kRfcSig1, &s, &why));
        CHECK(!ffb::parse_signature("<html>404</html>", &s, &why));
    }

    std::puts("signature_url: the file's own path plus .sig");
    {
        CHECK(ffb::signature_url("https://coopmods.com/games.json") == "https://coopmods.com/games.json.sig");
        CHECK(ffb::signature_url("https://mewgenics.coopmods.com/update/manifest.json") ==
              "https://mewgenics.coopmods.com/update/manifest.json.sig");
        CHECK(ffb::signature_url("https://h/m.json?x=1#y") == "https://h/m.json.sig");
    }

    std::puts("the exe trusts exactly the coopmods key docs/SPEC.md names");
    {
        const auto& keys = ffb::trusted_keys();
        CHECK(keys.size() == 1);
        CHECK(keys.size() == 1 && to_hex(keys[0].data(), 32) == kCoopmodsKey);
        // tools/signing.py (Python `cryptography`) signed this fixed sentence with the
        // coopmods private key on 2026-10-01: the publish side and the launcher side
        // agree on the format and the key. Not JSON, so it can never pass as a file.
        const std::string sentence =
            "ffb-coop#21 cross-check: signed by tools/signing.py, verified by src/signature.cpp";
        const std::string by_publish =
            "fe27d8ce7fa0b6e27b61680be4bb8a6fe0bf0f1a09de0c1110504fe09ce0fc77887285d67e205cf9804c323fa8d4ce1c81176f286defc4de93dc6e7d9a9c4a07\n";
        CHECK(ffb::check_signature("cross-check", sentence, by_publish, keys).empty());
        CHECK(!ffb::check_signature("cross-check", sentence + ".", by_publish, keys).empty());
        // ... and not the test key, so a fixture signature never passes in the real exe.
        CHECK(!ffb::check_signature("games.json", kGamesJson, sign(kGamesJson), keys).empty());
    }

    std::puts("parse_public_key: 64 hex digits only");
    {
        ffb::PublicKey k{};
        CHECK(ffb::parse_public_key(kCoopmodsKey, &k));
        CHECK(!ffb::parse_public_key(std::string(kCoopmodsKey).substr(2), &k));
        CHECK(!ffb::parse_public_key(std::string(kCoopmodsKey) + "00", &k));
        CHECK(!ffb::parse_public_key("x" + std::string(kCoopmodsKey).substr(1), &k));
    }

    four_cases("games.json", kGamesJson);
    four_cases("the manifest", kManifest);

    return ffb_test_result();
}
