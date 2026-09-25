// test_finder.cpp -- the game-folder finder (docs/SPEC.md, "The flow", step 3):
// empty folder, one match, two matches, and the rules around them (any case,
// the folder itself only, files only).
//
// Each case builds a real folder under the system temp directory and removes it
// afterwards; no game is needed, only files with the right names.
#include "finder.h"
#include "ffb_test.h"

#include <chrono>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static ffb::GameEntry game(const char* id, const char* exe) {
    ffb::GameEntry g;
    g.id = id;
    g.name = id;
    g.exe = exe;
    g.manifest = "https://example.coopmods.com/update/manifest.json";
    g.launcher = "loader.exe";
    return g;
}

static const std::vector<ffb::GameEntry> kGames = {game("mewgenics", "Mewgenics.exe"),
                                                   game("other", "Other.exe")};

// A fresh, empty folder that is removed when the value goes away.
struct TempFolder {
    fs::path path;
    TempFolder() {
        static int n = 0;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = fs::temp_directory_path() /
               ("ffb_test_finder_" + std::to_string(stamp) + "_" + std::to_string(n++));
        fs::create_directories(path);
    }
    ~TempFolder() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    void file(const std::string& name) const { std::ofstream(path / fs::u8path(name)) << "x"; }
    void folder(const std::string& name) const { fs::create_directories(path / fs::u8path(name)); }
};

static void test_empty_folder() {
    std::printf("empty folder\n");
    TempFolder t;
    const ffb::FindResult r = ffb::find_game(t.path, kGames);
    CHECK(r.outcome == ffb::FindOutcome::None);
    CHECK(r.matches.empty());
}

static void test_no_match() {
    std::printf("folder with other files only\n");
    TempFolder t;
    t.file("readme.txt");
    t.file("Mewgenics.exe.bak");
    t.file("Mewgenics");
    t.file("FFB Co-op.exe");
    CHECK(ffb::find_game(t.path, kGames).outcome == ffb::FindOutcome::None);
}

static void test_one_match() {
    std::printf("one match\n");
    TempFolder t;
    t.file("Mewgenics.exe");
    t.file("FFB Co-op.exe");
    t.file("steam_api64.dll");
    const ffb::FindResult r = ffb::find_game(t.path, kGames);
    CHECK(r.outcome == ffb::FindOutcome::One);
    CHECK(r.matches.size() == 1);
    CHECK(r.matches.size() == 1 && r.matches[0].id == "mewgenics");
    CHECK(r.matches.size() == 1 && r.matches[0].exe == "Mewgenics.exe");
}

static void test_one_match_any_case() {
    std::printf("one match, file name in another case\n");
    TempFolder t;
    t.file("MEWGENICS.EXE");
    const ffb::FindResult r = ffb::find_game(t.path, kGames);
    CHECK(r.outcome == ffb::FindOutcome::One);
    CHECK(r.matches.size() == 1 && r.matches[0].id == "mewgenics");
}

static void test_two_matches() {
    std::printf("two matches\n");
    TempFolder t;
    t.file("Other.exe");
    t.file("Mewgenics.exe");
    const ffb::FindResult r = ffb::find_game(t.path, kGames);
    CHECK(r.outcome == ffb::FindOutcome::Many);
    CHECK(r.matches.size() == 2);
    // In games.json order, so the screen names them the same way every time.
    CHECK(r.matches.size() == 2 && r.matches[0].exe == "Mewgenics.exe" && r.matches[1].exe == "Other.exe");
}

static void test_folder_itself_only() {
    std::printf("subfolders are not searched\n");
    TempFolder t;
    t.folder("bin");
    t.file("bin/Mewgenics.exe");
    t.folder("FFB Co-op");
    t.file("FFB Co-op/Other.exe");
    CHECK(ffb::find_game(t.path, kGames).outcome == ffb::FindOutcome::None);
}

static void test_directory_is_not_a_game() {
    std::printf("a folder named like the exe is not the game\n");
    TempFolder t;
    t.folder("Mewgenics.exe");
    CHECK(ffb::find_game(t.path, kGames).outcome == ffb::FindOutcome::None);
}

static void test_missing_folder() {
    std::printf("a folder that does not exist finds nothing\n");
    fs::path gone;
    {
        TempFolder t;
        gone = t.path;
    }
    CHECK(!fs::exists(gone));
    CHECK(ffb::find_game(gone, kGames).outcome == ffb::FindOutcome::None);
}

static void test_empty_game_list() {
    std::printf("an empty game list finds nothing\n");
    TempFolder t;
    t.file("Mewgenics.exe");
    CHECK(ffb::find_game(t.path, {}).outcome == ffb::FindOutcome::None);
}

int main() {
    test_empty_folder();
    test_no_match();
    test_one_match();
    test_one_match_any_case();
    test_two_matches();
    test_folder_itself_only();
    test_directory_is_not_a_game();
    test_missing_folder();
    test_empty_game_list();
    return ffb_test_result();
}
