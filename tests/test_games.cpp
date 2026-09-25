// test_games.cpp -- games.json parsing: every rule in docs/SPEC.md's
// "Validation, field by field" table and the "Network file-name rule", one case
// or more each, plus malformed input.
//
// Each bad case starts from the spec's own example file and breaks exactly one
// thing, so a failure names the rule that no longer holds. The expected field in
// the message comes from the spec's table, not from running the parser.
#include "games.h"
#include "plain_name.h"
#include "ffb_test.h"

#include "json.hpp"

#include <string>

using nlohmann::json;

// The example from docs/SPEC.md, "games.json" -> "Example".
static const char* kSpecExample = R"({
  "schema": 1,
  "launcher": {
    "version": "1.0.0",
    "url": "https://coopmods.com/launcher/FFB%20Co-op.exe",
    "sha256": "9f2c4e1a7b3d5f60812a4c6e8b0d2f4a6c8e0b2d4f6a8c0e2b4d6f8a0c2e4b6d",
    "size": 412160
  },
  "games": [
    {
      "id": "mewgenics",
      "name": "Mewgenics",
      "exe": "Mewgenics.exe",
      "steam_appid": 0,
      "manifest": "https://mewgenics.coopmods.com/update/manifest.json",
      "launcher": "mewcoop_loader.exe"
    }
  ]
})";

static json example() { return json::parse(kSpecExample); }

static json second_game() {
    return json{{"id", "other-game"}, {"name", "Other"}, {"exe", "Other.exe"},
                {"steam_appid", 12345}, {"manifest", "https://other.coopmods.com/update/manifest.json"},
                {"launcher", "other_loader.exe"}};
}

static bool starts_with(const std::string& s, const std::string& prefix) {
    return s.compare(0, prefix.size(), prefix) == 0;
}

// Invalid, the message opens with `field`, and nothing of the file is handed back.
static bool rejects_text(const std::string& text, const std::string& field) {
    const ffb::GamesJsonResult r = ffb::parse_games_json(text);
    const bool ok = !r.ok && starts_with(r.error, field + ":") && r.value.games.empty() &&
                    r.value.launcher.version.empty();
    if (!ok) std::printf("        got ok=%d error=\"%s\"\n", r.ok ? 1 : 0, r.error.c_str());
    return ok;
}
static bool rejects(const json& j, const std::string& field) { return rejects_text(j.dump(), field); }
static bool accepts(const json& j) {
    const ffb::GamesJsonResult r = ffb::parse_games_json(j.dump());
    if (!r.ok) std::printf("        got error=\"%s\"\n", r.error.c_str());
    return r.ok;
}

// j with `key` of the first game set to v.
static json with_game(const char* key, const json& v) {
    json j = example();
    j["games"][0][key] = v;
    return j;
}
static json with_launcher(const char* key, const json& v) {
    json j = example();
    j["launcher"][key] = v;
    return j;
}

static void test_spec_example_parses() {
    std::printf("spec example parses\n");
    const ffb::GamesJsonResult r = ffb::parse_games_json(kSpecExample);
    CHECK(r.ok);
    CHECK(r.error.empty());
    CHECK(r.value.launcher.version == "1.0.0");
    CHECK(r.value.launcher.url == "https://coopmods.com/launcher/FFB%20Co-op.exe");
    CHECK(r.value.launcher.sha256 == "9f2c4e1a7b3d5f60812a4c6e8b0d2f4a6c8e0b2d4f6a8c0e2b4d6f8a0c2e4b6d");
    CHECK(r.value.launcher.size == 412160);
    CHECK(r.value.games.size() == 1);
    if (r.value.games.size() == 1) {
        const ffb::GameEntry& g = r.value.games[0];
        CHECK(g.id == "mewgenics");
        CHECK(g.name == "Mewgenics");
        CHECK(g.exe == "Mewgenics.exe");
        CHECK(g.steam_appid == 0);
        CHECK(g.manifest == "https://mewgenics.coopmods.com/update/manifest.json");
        CHECK(g.launcher == "mewcoop_loader.exe");
    }

    json two = example();
    two["games"].push_back(second_game());
    const ffb::GamesJsonResult r2 = ffb::parse_games_json(two.dump());
    CHECK(r2.ok && r2.value.games.size() == 2);
    CHECK(r2.ok && r2.value.games[1].id == "other-game" && r2.value.games[1].steam_appid == 12345);
}

static void test_malformed() {
    std::printf("malformed games.json\n");
    CHECK(!ffb::parse_games_json("").ok);
    CHECK(!ffb::parse_games_json("{").ok);
    CHECK(!ffb::parse_games_json("not json").ok);
    CHECK(!ffb::parse_games_json(std::string(kSpecExample) + " trailing").ok);
    CHECK(!ffb::parse_games_json(std::string(kSpecExample).substr(0, 200)).ok);  // cut short
    CHECK(!ffb::parse_games_json("// comment\n" + std::string(kSpecExample)).ok);  // strict JSON
    CHECK(!ffb::parse_games_json("{\"schema\": 1,}").ok);                        // trailing comma
    CHECK(!ffb::parse_games_json("{\"schema\": 1, \"x\": \"\xff\"}").ok);           // not UTF-8
    CHECK(ffb::parse_games_json("{").error == "not valid JSON");
    CHECK(rejects_text("[]", "(top level)"));
    CHECK(rejects_text("\"games\"", "(top level)"));
    CHECK(rejects_text("null", "(top level)"));
}

static void test_schema() {
    std::printf("schema\n");
    json j = example();
    j.erase("schema");
    CHECK(rejects(j, "schema"));
    j = example(); j["schema"] = 2;    CHECK(rejects(j, "schema"));
    j = example(); j["schema"] = 0;    CHECK(rejects(j, "schema"));
    j = example(); j["schema"] = -1;   CHECK(rejects(j, "schema"));
    j = example(); j["schema"] = 1.0;  CHECK(rejects(j, "schema"));
    j = example(); j["schema"] = "1";  CHECK(rejects(j, "schema"));
}

static void test_launcher_block() {
    std::printf("launcher block\n");
    json j = example();
    j.erase("launcher");
    CHECK(rejects(j, "launcher"));
    j = example(); j["launcher"] = "1.0.0";          CHECK(rejects(j, "launcher"));
    j = example(); j["launcher"] = json::array();    CHECK(rejects(j, "launcher"));

    for (const char* key : {"version", "url", "sha256", "size"}) {
        j = example();
        j["launcher"].erase(key);
        CHECK(rejects(j, std::string("launcher.") + key));
    }

    // version: MAJOR.MINOR.PATCH, non-negative decimal integers, no sign.
    for (const char* bad : {"1.0", "1.0.0.0", "v1.0.0", "+1.0.0", "1.-1.0", "1..0", " 1.0.0",
                            "1.0.0 ", "1.0.x", "", "1,0,0", "99999999999999999999.0.0"})
        CHECK(rejects(with_launcher("version", bad), "launcher.version"));
    CHECK(rejects(with_launcher("version", 1), "launcher.version"));
    CHECK(accepts(with_launcher("version", "0.0.0")));
    CHECK(accepts(with_launcher("version", "1.10.0")));
    CHECK(accepts(with_launcher("version", "01.2.3")));  // decimal: a leading zero is still decimal

    // url: absolute https://.
    for (const char* bad : {"http://coopmods.com/launcher/x.exe", "coopmods.com/x.exe", "/launcher/x.exe",
                            "https://", "https:///x.exe", "ftp://coopmods.com/x.exe",
                            "https://coopmods.com/FFB Co-op.exe", ""})
        CHECK(rejects(with_launcher("url", bad), "launcher.url"));
    CHECK(rejects(with_launcher("url", 5), "launcher.url"));

    // sha256: exactly 64 hex digits, any case.
    const std::string hex64 = "9f2c4e1a7b3d5f60812a4c6e8b0d2f4a6c8e0b2d4f6a8c0e2b4d6f8a0c2e4b6d";
    CHECK(rejects(with_launcher("sha256", hex64.substr(0, 63)), "launcher.sha256"));
    CHECK(rejects(with_launcher("sha256", hex64 + "0"), "launcher.sha256"));
    CHECK(rejects(with_launcher("sha256", "g" + hex64.substr(1)), "launcher.sha256"));
    CHECK(rejects(with_launcher("sha256", ""), "launcher.sha256"));
    CHECK(accepts(with_launcher("sha256", "9F2C4E1A7B3D5F60812A4C6E8B0D2F4A6C8E0B2D4F6A8C0E2B4D6F8A0C2E4B6D")));

    // size: integer, at least 1.
    CHECK(rejects(with_launcher("size", 0), "launcher.size"));
    CHECK(rejects(with_launcher("size", -5), "launcher.size"));
    CHECK(rejects(with_launcher("size", 1.5), "launcher.size"));
    CHECK(rejects(with_launcher("size", "412160"), "launcher.size"));
    CHECK(accepts(with_launcher("size", 1)));
}

static void test_games_array() {
    std::printf("games array\n");
    json j = example();
    j.erase("games");
    CHECK(rejects(j, "games"));
    j = example(); j["games"] = json::array();        CHECK(rejects(j, "games"));
    j = example(); j["games"] = json::object();       CHECK(rejects(j, "games"));
    j = example(); j["games"] = second_game();        CHECK(rejects(j, "games"));
    j = example(); j["games"].push_back("Other.exe"); CHECK(rejects(j, "games[1]"));
}

static void test_game_entry_missing_field() {
    std::printf("a game entry missing a required field\n");
    for (const char* key : {"id", "name", "exe", "steam_appid", "manifest", "launcher"}) {
        json j = example();
        j["games"][0].erase(key);
        CHECK(rejects(j, std::string("games[0].") + key));
        // The same in the second entry: the message names which entry.
        j = example();
        json g = second_game();
        g.erase(key);
        j["games"].push_back(g);
        CHECK(rejects(j, std::string("games[1].") + key));
    }
}

static void test_game_fields() {
    std::printf("game entry fields\n");
    // id: 1-32 characters from a-z, 0-9 and -.
    for (const char* bad : {"", "Mewgenics", "mew_genics", "mew genics", "mew.genics",
                            "abcdefghijklmnopqrstuvwxyz0123456"})  // 33
        CHECK(rejects(with_game("id", bad), "games[0].id"));
    CHECK(rejects(with_game("id", 7), "games[0].id"));
    CHECK(accepts(with_game("id", "abcdefghijklmnopqrstuvwxyz012345")));  // 32
    CHECK(accepts(with_game("id", "a")));
    CHECK(accepts(with_game("id", "game-2")));

    // name: a non-empty string.
    CHECK(rejects(with_game("name", ""), "games[0].name"));
    CHECK(rejects(with_game("name", 5), "games[0].name"));
    CHECK(accepts(with_game("name", "Mewgenics: Director's Cut")));  // shown, never a path

    // exe: a plain file name ending in .exe, any case.
    for (const char* bad : {"Mewgenics", "Mewgenics.dll", "Mewgenics.exe.txt", "sub/Mewgenics.exe",
                            "..\\Mewgenics.exe", "C:Mewgenics.exe", "CON.exe", ".exe."})
        CHECK(rejects(with_game("exe", bad), "games[0].exe"));
    CHECK(rejects(with_game("exe", 1), "games[0].exe"));
    CHECK(accepts(with_game("exe", "MEWGENICS.EXE")));
    CHECK(accepts(with_game("exe", "Mewgenics.Exe")));

    // steam_appid: an integer, at least 0.
    CHECK(rejects(with_game("steam_appid", -1), "games[0].steam_appid"));
    CHECK(rejects(with_game("steam_appid", "0"), "games[0].steam_appid"));
    CHECK(rejects(with_game("steam_appid", 1.5), "games[0].steam_appid"));
    CHECK(accepts(with_game("steam_appid", 0)));
    CHECK(accepts(with_game("steam_appid", 3040180)));

    // manifest: absolute https://.
    CHECK(rejects(with_game("manifest", "http://mewgenics.coopmods.com/update/manifest.json"),
                  "games[0].manifest"));
    CHECK(rejects(with_game("manifest", "update/manifest.json"), "games[0].manifest"));
    CHECK(rejects(with_game("manifest", ""), "games[0].manifest"));

    // launcher: a plain file name ending in .exe.
    for (const char* bad : {"mewcoop_loader", "mewcoop.dll", "bin/mewcoop_loader.exe",
                            "..\\..\\mewcoop_loader.exe", "nul.exe", "", "mewcoop_loader.exe "})
        CHECK(rejects(with_game("launcher", bad), "games[0].launcher"));
    CHECK(accepts(with_game("launcher", "MEWCOOP_LOADER.EXE")));
}

static void test_uniqueness() {
    std::printf("id and exe unique across games\n");
    json j = example();
    json g = second_game();
    g["id"] = "mewgenics";
    j["games"].push_back(g);
    CHECK(rejects(j, "games[1].id"));

    j = example();
    g = second_game();
    g["exe"] = "MEWGENICS.exe";  // compared without regard to case
    j["games"].push_back(g);
    CHECK(rejects(j, "games[1].exe"));

    // A shared launcher name is fine: each lives in its own game's package folder.
    j = example();
    g = second_game();
    g["launcher"] = "mewcoop_loader.exe";
    j["games"].push_back(g);
    CHECK(accepts(j));
}

static void test_unknown_keys_ignored() {
    std::printf("unknown keys are ignored\n");
    json j = example();
    j["future"] = {{"anything", true}};
    j["launcher"]["notes"] = "x";
    j["games"][0]["icon"] = "https://coopmods.com/mew.png";
    CHECK(accepts(j));
}

static void test_one_broken_rule_invalidates_all() {
    std::printf("one broken rule makes the whole file invalid\n");
    json j = example();
    json g = second_game();
    g["exe"] = "../Other.exe";
    j["games"].push_back(g);  // games[0] is fine, games[1] is not
    const ffb::GamesJsonResult r = ffb::parse_games_json(j.dump());
    CHECK(!r.ok);
    CHECK(r.value.games.empty());
    // The message names the rejected name.
    CHECK(r.error.find("../Other.exe") != std::string::npos);
}

static void test_parse_version() {
    std::printf("parse_version\n");
    std::uint64_t v[3] = {9, 9, 9};
    CHECK(ffb::parse_version("1.10.0", v) && v[0] == 1 && v[1] == 10 && v[2] == 0);
    CHECK(ffb::parse_version("0.1.0", v) && v[0] == 0 && v[1] == 1 && v[2] == 0);
    CHECK(ffb::parse_version("18446744073709551615.0.0", v) && v[0] == 18446744073709551615ull);
    CHECK(!ffb::parse_version("18446744073709551616.0.0", v));  // one past UINT64_MAX
    CHECK(!ffb::parse_version("1.0", v));
    CHECK(!ffb::parse_version("1.0.0-beta", v));
}

static void test_plain_file_name() {
    std::printf("network file-name rule\n");
    // Good names.
    for (const char* ok : {"Mewgenics.exe", "mewcoop_loader.exe", "mewcoop_ui.swf", "FFB Co-op.exe",
                           "a", "a.b.c", "CONSOLE.exe", "COM0.exe", "COM10.exe", "LPT.exe", "nulx"})
        CHECK(ffb::is_plain_file_name(ok));
    CHECK(ffb::is_plain_file_name(std::string(255, 'a')));
    // 255 characters of two bytes each is still 255 characters.
    std::string two_byte;
    for (int i = 0; i < 255; ++i) two_byte += "\xc3\xa9";
    CHECK(ffb::is_plain_file_name(two_byte));

    // Bad names, one rule each.
    CHECK(!ffb::is_plain_file_name(""));
    CHECK(!ffb::is_plain_file_name(std::string(256, 'a')));
    CHECK(!ffb::is_plain_file_name(two_byte + "\xc3\xa9"));
    CHECK(!ffb::is_plain_file_name("a/b.exe"));
    CHECK(!ffb::is_plain_file_name("a\\b.exe"));
    CHECK(!ffb::is_plain_file_name("C:b.exe"));
    CHECK(!ffb::is_plain_file_name("a.exe:stream"));
    CHECK(!ffb::is_plain_file_name(std::string("a\x01.exe")));
    CHECK(!ffb::is_plain_file_name(std::string("a\x1f.exe")));
    CHECK(!ffb::is_plain_file_name(std::string("a\tb.exe")));
    CHECK(!ffb::is_plain_file_name("."));
    CHECK(!ffb::is_plain_file_name(".."));
    CHECK(!ffb::is_plain_file_name("a..b.exe"));
    CHECK(!ffb::is_plain_file_name("..a.exe"));
    CHECK(!ffb::is_plain_file_name(" a.exe"));
    CHECK(!ffb::is_plain_file_name("a.exe "));
    CHECK(!ffb::is_plain_file_name("a.exe."));
    for (const char* dev : {"CON", "PRN", "AUX", "NUL", "con", "Nul", "COM1", "com9", "LPT1", "lpt9",
                            "CON.exe", "nul.txt", "aux.tar.gz", "COM3.dll"})
        CHECK(!ffb::is_plain_file_name(dev));
    // Windows drops trailing spaces from the part before the first dot, so these open the device.
    for (const char* dev : {"nul .exe", "CON .txt", "com1  .dll", "LPT9 .a.b"})
        CHECK(!ffb::is_plain_file_name(dev));

    std::string why;
    CHECK(!ffb::is_plain_file_name("a/b", &why) && why == "contains /");
}

int main() {
    test_spec_example_parses();
    test_malformed();
    test_schema();
    test_launcher_block();
    test_games_array();
    test_game_entry_missing_field();
    test_game_fields();
    test_uniqueness();
    test_unknown_keys_ignored();
    test_one_broken_rule_invalidates_all();
    test_parse_version();
    test_plain_file_name();
    return ffb_test_result();
}
