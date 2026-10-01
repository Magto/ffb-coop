// trusted_keys.h -- the ed25519 public keys whose signatures FFB Co-op.exe
// accepts on games.json and on a game's manifest (docs/SPEC.md, "Signatures").
//
// One 64-hex-digit public key per line, each with the date it was made. A file
// signed by any key in this list is accepted. Rotation (docs/SPEC.md): add the
// new key here and ship that launcher while the old key still signs; switch
// signing to the new key; drop the old key in a later launcher.
//
// tools/signing.py reads this file and refuses to sign with a private key whose
// public key is not listed here, so nothing is ever published that the
// launcher would reject. Keep the one-string-per-line shape it reads.
#pragma once

namespace ffb {

inline const char* const kTrustedKeyHex[] = {
    "43706304f0fae090f7a2afd96e06d207ab193ea52a527d6576073ce281b30a59",  // coopmods 2026-10-01
};

}  // namespace ffb
