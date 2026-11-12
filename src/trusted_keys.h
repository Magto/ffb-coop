// trusted_keys.h -- the ed25519 public keys whose signatures FFB Co-op.exe
// accepts on games.json and on a game's manifest, and the names each key may
// sign (docs/SPEC.md, "Signatures").
//
// One row per key and name: the 64-hex-digit public key, then the "signed_as"
// name it may sign (#23). "*" is every name: games.json, the dev games.json and
// every game's manifest. A game's own key gets "<games[].id> manifest" and
// nothing else, so it can never sign games.json or another game's manifest; a
// key that may sign two names has two rows. Each row says who the key is and
// the date it was made.
//
// Rotation (docs/SPEC.md): add the new key here and ship that launcher while
// the old key still signs; switch signing to the new key; drop the old key in a
// later launcher.
//
// tools/signing.py reads this file and refuses to sign with a private key whose
// public key is not listed here, or a name its rows do not allow, so nothing is
// ever published that the launcher would reject. Keep the one-row-per-line
// shape it reads: {"<64 hex digits>", "<name or *>"},
#pragma once

namespace ffb {

struct TrustedKeyRow {
    const char* hex;       // the public key, 64 hex digits
    const char* may_sign;  // a "signed_as" name, or "*" for every name
};

inline const TrustedKeyRow kTrustedKeys[] = {
    {"43706304f0fae090f7a2afd96e06d207ab193ea52a527d6576073ce281b30a59", "*"},  // coopmods (Martin) 2026-10-01
    {"1283a33131f8652a3d75b124f5658d5cb7cc0c628e03243951dc9af8fb81e839", "theyarebillions manifest"},  // They Are Billions co-op (Simon) 2026-10-31
};

}  // namespace ffb
