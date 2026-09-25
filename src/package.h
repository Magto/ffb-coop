// package.h -- download or update a game's package into FFB Co-op\ from its
// manifest (docs/SPEC.md, "The game manifest" and "Package download"; #4).
//
// The manifest is the one the Mewgenics loader already reads:
//   { "version": "76.0.0", "wire": 34,
//     "files": [ { "name": "mewcoop.dll", "size": 4194304, "sha256": "...", "required": true } ] }
// Each file is fetched from the manifest's URL with its last path segment
// replaced by the file's name.
//
// The shape of a run, ported from Magto/mewgenics-coop loader/mewcoop_update.cpp
// (MIT, Copyright (c) 2026 SanTertrust; Copyright (c) 2026 Martin Strålenhielm for
// the Mewgenics Coop fork), with the spec's differences:
//   1. Fetch and validate the manifest. One broken rule rejects it whole and
//      nothing is downloaded.
//   2. Hash every file already in the package folder; a file whose sha256
//      matches is left alone. The manifest's version decides nothing.
//   3. Download each file that differs to <name>.new, checking size and sha256 as
//      the bytes arrive. A required file that fails: every .new is deleted and
//      nothing is replaced. An optional file that fails: deleted, a warning, and
//      the run goes on without it. Optional files are fetched on a fresh install
//      too (Martin's decision "Always fetch", unlike the Mewgenics loader).
//   4. Only then move every .new over its old file. One that cannot be replaced
//      (the game holds it open) leaves the old file and says which and why.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "net.h"

namespace ffb {

struct ManifestFile {
    std::string   name;        // a plain file name (plain_name.h)
    std::uint64_t size = 0;    // at least 1
    std::string   sha256;      // 64 hex digits, as the manifest wrote them
    bool          required = true;
};

struct Manifest {
    std::string               version;   // shown to the player, never compared
    std::vector<ManifestFile> files;
};

// Validates `text` against every manifest rule in docs/SPEC.md. -> false with
// `why` set to the first rule it broke; `out` is then untouched.
bool parse_manifest(const std::string& text, Manifest* out, std::string* why);

// "https://h/update/manifest.json" + "mewcoop.dll" -> "https://h/update/mewcoop.dll".
// The name is percent-encoded, so a space or a # in it stays part of the name.
std::string sibling_url(const std::string& manifest_url, const std::string& name);

enum class PackageOutcome {
    Current,      // every required file in the folder matches the manifest now
    Unreachable,  // the manifest could not be fetched (no network, HTTP error)
    Invalid,      // the manifest broke a rule; nothing was downloaded
    Failed,       // a required file failed to download, check or replace
};

struct PackageResult {
    PackageOutcome           outcome = PackageOutcome::Failed;
    std::string              reason;    // one line, for every outcome but Current
    std::string              version;   // the manifest's version, once it parsed
    std::vector<std::string> updated;   // files replaced or newly installed this run
    std::vector<std::string> warnings;  // optional files that failed; never block the start
    // Once the manifest parsed (Current and Failed; Invalid when only the
    // launcher rule broke): its text, which the start flow keeps for offline
    // starts, and what it lists.
    std::string              manifest_text;
    Manifest                 manifest;
};

// Brings `package_dir` (UTF-8; created when missing) up to date with the
// manifest at `manifest_url`. `required_launcher`, when not empty, must be one
// of the manifest's files and marked required (docs/SPEC.md, games[].launcher),
// or the manifest is Invalid. Progress and warnings are printed to `log`, one
// line each; nullptr is silent. Nothing is written outside `package_dir`.
PackageResult update_package(const std::string& package_dir, const std::string& manifest_url,
                             const std::string& required_launcher, Net& net,
                             std::FILE* log = stdout);

}  // namespace ffb
