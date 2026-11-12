// games.h -- games.json, parsed and validated (docs/SPEC.md, "games.json").
//
// A plain function over the file's text: no network, no disk, so every
// validation rule is a unit test (tests/test_games.cpp). One broken rule makes
// the whole file invalid -- the caller never gets part of a file.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ffb {

// The launcher's own update block.
struct LauncherInfo {
    std::string version;  // MAJOR.MINOR.PATCH
    std::string url;      // absolute https://
    std::string sha256;   // 64 hex digits, as sent
    std::uint64_t size = 0;
};

struct GameEntry {
    std::string id;
    std::string name;
    std::string exe;       // plain file name ending in .exe
    std::uint64_t steam_appid = 0;
    std::string manifest;  // absolute https://
    std::string launcher;  // plain file name ending in .exe
};

struct GamesJson {
    // What the file says it is (#23, signature.h): "" when it has no "signed_as".
    // Optional here, so a games.json kept by an older launcher still reads
    // offline; the start flow requires it on a fetched file.
    std::string signed_as;
    LauncherInfo launcher;
    std::vector<GameEntry> games;
};

struct GamesJsonResult {
    bool ok = false;
    std::string error;  // when !ok: what is wrong, naming the field, e.g. "games[0].exe: missing"
    GamesJson value;    // when ok
};

// Parses and validates games.json. The only schema this launcher reads is 1.
GamesJsonResult parse_games_json(const std::string& text);

// Parses "MAJOR.MINOR.PATCH" (each part a non-negative decimal integer, no sign)
// into out[0..2]. False on anything else. Newer is compared part by part as numbers.
bool parse_version(const std::string& s, std::uint64_t out[3]);

}  // namespace ffb
