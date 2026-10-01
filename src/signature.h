// signature.h -- the ed25519 signature check on games.json and on a game's
// manifest (#21, docs/SPEC.md "Signatures").
//
// The sha256 values in those files catch a broken download, not a hostile
// server: whoever controls coopmods.com could publish new bytes and a matching
// hash. So each file is signed offline, with a private key that is in no repo
// and not on the server, and the launcher checks the signature against the
// public keys compiled into it (src/trusted_keys.h) before it reads a byte of
// the file.
//
// The format: `<file>.sig`, served beside the file (games.json ->
// games.json.sig), is 128 hex digits -- the raw 64-byte ed25519 signature
// (RFC 8032) over the exact bytes of the file -- and may end in a newline.
// The file itself is unchanged, so a launcher without this check reads it as
// before.
//
// The ed25519 arithmetic is TweetNaCl (third_party/tweetnacl.c, public domain).
#pragma once

#include <array>
#include <string>
#include <vector>

namespace ffb {

using PublicKey = std::array<unsigned char, 32>;
using Signature = std::array<unsigned char, 64>;

// The keys in src/trusted_keys.h: the ones every real start checks against.
const std::vector<PublicKey>& trusted_keys();

// "https://h/games.json" -> "https://h/games.json.sig". A query or fragment is
// dropped first, so the .sig always names the file's own path.
std::string signature_url(const std::string& url);

// The text of a .sig file -> the signature. 128 hex digits, either case, then
// nothing but optional whitespace. -> false with `why` set on anything else.
bool parse_signature(const std::string& text, Signature* out, std::string* why);

// True when `sig` is a valid ed25519 signature of exactly `data` by `key`.
bool ed25519_verify(const Signature& sig, const std::string& data, const PublicKey& key);

// The whole check: `sig_text` (the .sig file) parses and signs exactly `data`
// under one of `keys`. -> empty when it does, else why not (one line, naming
// `what`, e.g. "games.json: the signature does not match").
std::string check_signature(const std::string& what, const std::string& data,
                            const std::string& sig_text, const std::vector<PublicKey>& keys);

// 64 hex digits -> a key. False on anything else.
bool parse_public_key(const std::string& hex, PublicKey* out);

}  // namespace ffb
