// games.cpp -- see games.h. The rules are docs/SPEC.md's "Validation, field by
// field" table, in its order; unknown keys are ignored everywhere.
#include "games.h"

#include "plain_name.h"

#include <set>

#include "json.hpp"

namespace ffb {

using nlohmann::json;

namespace {

// Thrown by the checks below and caught once in parse_games_json, so each rule
// is one line and the first broken rule is the message.
struct Invalid {
    std::string what;
};

[[noreturn]] void invalid(const std::string& field, const std::string& why) {
    throw Invalid{field + ": " + why};
}

// A pointer, not a reference: `field` is often a temporary, and GCC's
// -Wdangling-reference cannot tell the result does not point into it.
const json* require(const json& obj, const char* key, const std::string& field) {
    auto it = obj.find(key);
    if (it == obj.end()) invalid(field, "missing");
    return &*it;
}

std::string require_string(const json& obj, const char* key, const std::string& field) {
    const json& v = *require(obj, key, field);
    if (!v.is_string()) invalid(field, "must be a string");
    return v.get<std::string>();
}

// nlohmann keeps a non-negative integer literal as number_unsigned and a
// negative one as number_integer; 1.0 is a float and is not an integer here.
std::uint64_t require_uint(const json& obj, const char* key, const std::string& field,
                           std::uint64_t min) {
    const json& v = *require(obj, key, field);
    if (!v.is_number_integer()) invalid(field, "must be an integer");
    if (!v.is_number_unsigned()) invalid(field, "must be at least " + std::to_string(min));
    const std::uint64_t n = v.get<std::uint64_t>();
    if (n < min) invalid(field, "must be at least " + std::to_string(min));
    return n;
}

bool is_https_url(const std::string& s) {
    static const std::string scheme = "https://";
    if (s.size() <= scheme.size() || ascii_lower(s.substr(0, scheme.size())) != scheme)
        return false;
    for (unsigned char c : s)
        if (c <= 0x20 || c == 0x7F) return false;
    const char host0 = s[scheme.size()];
    return host0 != '/' && host0 != '?' && host0 != '#';
}

std::string require_https_url(const json& obj, const char* key, const std::string& field) {
    std::string s = require_string(obj, key, field);
    if (!is_https_url(s)) invalid(field, "\"" + s + "\" is not an absolute https:// URL");
    return s;
}

bool is_sha256_hex(const std::string& s) {
    if (s.size() != 64) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    return true;
}

bool is_game_id(const std::string& s) {
    if (s.empty() || s.size() > 32) return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    return true;
}

std::string require_exe_name(const json& obj, const char* key, const std::string& field) {
    std::string s = require_string(obj, key, field);
    std::string why;
    if (!is_plain_file_name(s, &why)) invalid(field, "\"" + s + "\" is not a plain file name (" + why + ")");
    if (!ends_with_exe(s)) invalid(field, "\"" + s + "\" does not end in .exe");
    return s;
}

LauncherInfo read_launcher(const json& root) {
    const json& l = *require(root, "launcher", "launcher");
    if (!l.is_object()) invalid("launcher", "must be an object");
    LauncherInfo out;
    out.version = require_string(l, "version", "launcher.version");
    std::uint64_t parts[3];
    if (!parse_version(out.version, parts))
        invalid("launcher.version", "\"" + out.version + "\" is not MAJOR.MINOR.PATCH");
    out.url = require_https_url(l, "url", "launcher.url");
    out.sha256 = require_string(l, "sha256", "launcher.sha256");
    if (!is_sha256_hex(out.sha256)) invalid("launcher.sha256", "must be exactly 64 hex digits");
    out.size = require_uint(l, "size", "launcher.size", 1);
    return out;
}

GameEntry read_game(const json& g, const std::string& at) {
    if (!g.is_object()) invalid(at, "must be an object");
    GameEntry out;
    out.id = require_string(g, "id", at + ".id");
    if (!is_game_id(out.id))
        invalid(at + ".id", "\"" + out.id + "\" must be 1-32 characters from a-z, 0-9 and -");
    out.name = require_string(g, "name", at + ".name");
    if (out.name.empty()) invalid(at + ".name", "is empty");
    out.exe = require_exe_name(g, "exe", at + ".exe");
    out.steam_appid = require_uint(g, "steam_appid", at + ".steam_appid", 0);
    out.manifest = require_https_url(g, "manifest", at + ".manifest");
    out.launcher = require_exe_name(g, "launcher", at + ".launcher");
    return out;
}

}  // namespace

bool parse_version(const std::string& s, std::uint64_t out[3]) {
    std::size_t i = 0;
    for (int part = 0; part < 3; ++part) {
        if (part > 0) {
            if (i >= s.size() || s[i] != '.') return false;
            ++i;
        }
        const std::size_t start = i;
        std::uint64_t n = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            const std::uint64_t d = static_cast<std::uint64_t>(s[i] - '0');
            if (n > (UINT64_MAX - d) / 10) return false;  // does not fit
            n = n * 10 + d;
            ++i;
        }
        if (i == start) return false;
        out[part] = n;
    }
    return i == s.size();
}

GamesJsonResult parse_games_json(const std::string& text) {
    GamesJsonResult r;
    // No exceptions, no comments: strict JSON, and a parse error is a discarded value.
    const json root = json::parse(text, nullptr, false, false);
    if (root.is_discarded()) {
        r.error = "not valid JSON";
        return r;
    }
    try {
        if (!root.is_object()) invalid("(top level)", "must be an object");

        const json& schema = *require(root, "schema", "schema");
        if (!schema.is_number_integer()) invalid("schema", "must be an integer");
        if (!schema.is_number_unsigned() || schema.get<std::uint64_t>() != 1)
            invalid("schema", schema.dump() + " is not a format this launcher knows (only 1)");

        r.value.launcher = read_launcher(root);

        const json& games = *require(root, "games", "games");
        if (!games.is_array()) invalid("games", "must be an array");
        if (games.empty()) invalid("games", "has no entries");
        std::set<std::string> ids, exes;
        for (std::size_t i = 0; i < games.size(); ++i) {
            const std::string at = "games[" + std::to_string(i) + "]";
            GameEntry g = read_game(games[i], at);
            if (!ids.insert(g.id).second) invalid(at + ".id", "\"" + g.id + "\" appears twice");
            if (!exes.insert(ascii_lower(g.exe)).second)
                invalid(at + ".exe", "\"" + g.exe + "\" appears twice");
            r.value.games.push_back(std::move(g));
        }
    } catch (const Invalid& e) {
        r.value = GamesJson{};
        r.error = e.what;
        return r;
    }
    r.ok = true;
    return r;
}

}  // namespace ffb
