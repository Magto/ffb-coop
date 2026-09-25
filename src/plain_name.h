// plain_name.h -- the network file-name rule (docs/SPEC.md, "Network file-name rule").
//
// Every name that arrives over the network and becomes a file name -- games[].exe,
// games[].launcher, a manifest's files[].name -- must pass this check before it is
// joined to a path. One function, so games.json (#3) and the package manifest (#4)
// cannot drift apart on what "plain" means.
#pragma once
#include <string>

namespace ffb {

// True when `name` (UTF-8) is a plain file name. On false, `why` (when given) is
// set to a short reason, e.g. "contains \\".
bool is_plain_file_name(const std::string& name, std::string* why = nullptr);

// True when `name` ends in ".exe", in any case.
bool ends_with_exe(const std::string& name);

// ASCII lower-case copy; the comparisons the spec calls "without regard to case".
// ASCII only, on purpose: NTFS also folds non-ASCII letters (Ü/ü), but every exe name
// a supported game uses is ASCII, so exe matching and uniqueness fold A-Z alone.
std::string ascii_lower(const std::string& s);

}  // namespace ffb
