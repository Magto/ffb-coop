// test_app.cpp -- the start flow (#6, docs/SPEC.md "The flow", "Error screens",
// "Offline"): every screen, the happy path, the offline starts and the
// self-update outcomes, with a fake network, a fake self-update, a fake process
// start and a fake key press. The game folder is a real temporary folder with a
// space in its name, because the finder and the package update (#3, #4) work on
// real files.
//
// The screen texts asserted here are copied from docs/SPEC.md, not from a run.
#include "ffb_test.h"
#include "app.h"
#include "net.h"

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

const std::string kManifestUrl = "https://example.test/update/manifest.json";
const std::string kBase        = "https://example.test/update/";
const std::string kSelfUrl     = "https://coopmods.com/launcher/FFB%20Co-op.exe";
const std::string kLoader      = "mewcoop_loader.exe";
const std::string kDll         = "mewcoop.dll";

struct FakeNet : ffb::Net {
    std::map<std::string, std::pair<int, std::string>> pages;   // url -> status, body
    std::vector<std::string> asked;
    int get(const std::string& url, const ffb::Sink& sink) override {
        asked.push_back(url);
        auto it = pages.find(url);
        if (it == pages.end()) return 404;   // coopmods.com/games.json today
        if (it->second.first == 200) sink(it->second.second.data(), it->second.second.size());
        return it->second.first;
    }
};

struct FakeSelfIo : ffb::SelfUpdateIo {
    bool download_ok = false;
    std::string served;   // the bytes a successful download "wrote"
    int downloads = 0, restarts = 0;
    std::vector<std::string> warns;
    bool is_restarted_child() override { return false; }
    std::wstring self_path() override { return L"C:\\Games\\FFB Co-op.exe"; }
    bool download(const std::string&, const std::wstring&, std::string* hex, std::uint64_t* got) override {
        ++downloads;
        if (!download_ok) return false;
        *hex = ffb::sha256_hex(served);
        *got = served.size();
        return true;
    }
    bool move_replace(const std::wstring&, const std::wstring&) override { return true; }
    void remove(const std::wstring&) override {}
    bool restart(const std::wstring&) override { ++restarts; return true; }
    void note(const std::string&) override {}
    void warn(const std::string& line) override { warns.push_back(line); }
};

struct Start { std::string exe, cmdline, workdir; };

struct FakeIo : ffb::AppIo {
    FakeNet net_;
    FakeSelfIo self_;
    std::vector<Start> starts;
    bool start_ok = true;
    int keys = 0;
    std::vector<std::string> outs, errs;

    ffb::Net& net() override { return net_; }
    ffb::SelfUpdateIo& self_update_io() override { return self_; }
    bool start_process(const std::string& exe, const std::string& cmdline, const std::string& workdir,
                       std::string* why) override {
        starts.push_back({exe, cmdline, workdir});
        if (!start_ok) *why = "error 2";
        return start_ok;
    }
    void wait_key() override { ++keys; }
    void out(const std::string& l) override { outs.push_back(l); }
    void err(const std::string& l) override { errs.push_back(l); }
    std::FILE* package_log() override { return nullptr; }
};

struct TempFolder {
    fs::path path;
    TempFolder() {
        static int n = 0;
        path = fs::temp_directory_path() /
               ("ffb app test " + std::to_string(std::time(nullptr)) + "-" + std::to_string(std::rand()) +
                "-" + std::to_string(++n));
        fs::create_directories(path);
    }
    ~TempFolder() { std::error_code ec; fs::remove_all(path, ec); }
    std::string str() const { return path.u8string(); }
    fs::path pkg() const { return path / "FFB Co-op"; }
};

void write(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << s;
}

std::string read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

const std::string kSha0(64, '0');

std::string game_json(const std::string& id, const std::string& exe) {
    return "{\"id\":\"" + id + "\",\"name\":\"" + id + "\",\"exe\":\"" + exe +
           "\",\"steam_appid\":0,\"manifest\":\"" + kManifestUrl + "\",\"launcher\":\"" + kLoader + "\"}";
}

// games.json with the launcher block at `version` and the two games below.
std::string games_json(const std::string& version = "0.1.0", const std::string& sha = kSha0,
                       std::uint64_t size = 1) {
    return "{\"schema\":1,\"launcher\":{\"version\":\"" + version + "\",\"url\":\"" + kSelfUrl +
           "\",\"sha256\":\"" + sha + "\",\"size\":" + std::to_string(size) + "},\"games\":[" +
           game_json("mewgenics", "Mewgenics.exe") + "," + game_json("other", "Other.exe") + "]}";
}

const std::string kLoaderBytes = "loader bytes";
const std::string kDllBytes    = "dll bytes";

std::string manifest() {
    auto f = [](const std::string& name, const std::string& body) {
        return "{\"name\":\"" + name + "\",\"size\":" + std::to_string(body.size()) + ",\"sha256\":\"" +
               ffb::sha256_hex(body) + "\",\"required\":true}";
    };
    return "{\"version\":\"76.0.0\",\"wire\":34,\"files\":[" + f(kLoader, kLoaderBytes) + "," +
           f(kDll, kDllBytes) + "]}";
}

void serve_all(FakeIo& io) {
    io.net_.pages[ffb::kGamesJsonUrl] = {200, games_json()};
    io.net_.pages[kManifestUrl]       = {200, manifest()};
    io.net_.pages[kBase + kLoader]    = {200, kLoaderBytes};
    io.net_.pages[kBase + kDll]       = {200, kDllBytes};
}

ffb::AppInput input(const TempFolder& t, const std::string& tail = "") {
    ffb::AppInput in;
    in.game_folder = t.str();
    in.arg_tail    = tail;
    in.running     = {0, 1, 0};
    return in;
}

// What an earlier online start leaves behind: games.json kept and the launcher installed.
void install(const TempFolder& t, bool with_launcher = true) {
    write(t.pkg() / "games.json", games_json());
    if (with_launcher) write(t.pkg() / kLoader, kLoaderBytes);
}

std::string q(const fs::path& p) { return "\"" + p.u8string() + "\""; }

bool has(const std::vector<std::string>& lines, const std::string& l) {
    for (const auto& x : lines) if (x == l) return true;
    return false;
}

void test_happy_path() {
    std::printf("happy path: package lands in FFB Co-op\\, the launcher starts with the game path\n");
    TempFolder t;
    write(t.path / "Mewgenics.exe", "game");
    FakeIo io;
    serve_all(io);
    const int rc = ffb::run_app(input(t, "-windowed \"save slot 2\""), io);
    CHECK(rc == 0);
    CHECK(io.outs.size() >= 1 && io.outs[0].rfind("FFB Co-op ", 0) == 0);
    CHECK(read(t.pkg() / kLoader) == kLoaderBytes);
    CHECK(read(t.pkg() / kDll) == kDllBytes);
    CHECK(read(t.pkg() / "games.json") == games_json());   // kept for offline starts
    CHECK(io.starts.size() == 1);
    if (io.starts.size() == 1) {
        CHECK(io.starts[0].exe == (t.pkg() / kLoader).u8string());
        CHECK(io.starts[0].workdir == t.pkg().u8string());   // accepted default 7
        // The temp folder name has spaces, so both paths arrive quoted; the
        // extra arguments follow unchanged.
        CHECK(io.starts[0].cmdline ==
              q(t.pkg() / kLoader) + " " + q(t.path / "Mewgenics.exe") + " -windowed \"save slot 2\"");
    }
    CHECK(io.keys == 0);
    CHECK(io.errs.empty());
}

void test_already_current() {
    std::printf("package already current: nothing downloaded, the launcher starts\n");
    TempFolder t;
    write(t.path / "Mewgenics.exe", "game");
    write(t.pkg() / kLoader, kLoaderBytes);
    write(t.pkg() / kDll, kDllBytes);
    FakeIo io;
    serve_all(io);
    CHECK(ffb::run_app(input(t), io) == 0);
    CHECK(io.net_.asked.size() == 2);   // games.json and the manifest, no file
    CHECK(io.starts.size() == 1);
    if (io.starts.size() == 1)
        CHECK(io.starts[0].cmdline == q(t.pkg() / kLoader) + " " + q(t.path / "Mewgenics.exe"));
}

void test_none_found() {
    std::printf("no supported game: the none screen, a key press, exit 1\n");
    TempFolder t;
    write(t.path / "readme.txt", "x");
    FakeIo io;
    serve_all(io);
    CHECK(ffb::run_app(input(t), io) == 1);
    CHECK(io.errs.size() == 4);
    if (io.errs.size() == 4) {
        CHECK(io.errs[0] == "Could not find a FFB modded game in " + t.str());
        CHECK(io.errs[1] == "Looking for: Mewgenics.exe, Other.exe");
        CHECK(io.errs[2] == "Put FFB Co-op.exe next to the game's exe and start it again.");
        CHECK(io.errs[3] == "Press any key to exit.");
    }
    CHECK(io.keys == 1);
    CHECK(io.starts.empty());
    CHECK(!fs::exists(t.pkg()));   // nothing written for a folder with no game
}

void test_two_found() {
    std::printf("two supported games: the two-games screen naming both, a key press, exit 1\n");
    TempFolder t;
    write(t.path / "Mewgenics.exe", "game");
    write(t.path / "OTHER.EXE", "game");
    FakeIo io;
    serve_all(io);
    CHECK(ffb::run_app(input(t), io) == 1);
    CHECK(io.errs.size() == 3);
    if (io.errs.size() == 3) {
        CHECK(io.errs[0] == "Found more than one FFB modded game in " + t.str() + ": Mewgenics.exe, Other.exe");
        CHECK(io.errs[1] == "FFB Co-op.exe can only serve one game per folder.");
        CHECK(io.errs[2] == "Press any key to exit.");
    }
    CHECK(io.keys == 1);
    CHECK(io.starts.empty());
}

void test_offline_installed() {
    std::printf("server unreachable (games.json 404), package installed: one warning, the launcher starts\n");
    TempFolder t;
    write(t.path / "Mewgenics.exe", "game");
    install(t);
    FakeIo io;   // serves nothing: every URL is a 404
    CHECK(ffb::run_app(input(t, "-x"), io) == 0);
    CHECK(io.errs.size() == 1);
    if (io.errs.size() == 1)
        CHECK(io.errs[0] == "Could not reach coopmods.com -- starting the installed version.");
    CHECK(io.keys == 0);
    CHECK(io.starts.size() == 1);
    if (io.starts.size() == 1)
        CHECK(io.starts[0].cmdline == q(t.pkg() / kLoader) + " " + q(t.path / "Mewgenics.exe") + " -x");
    CHECK(io.self_.downloads == 0);   // no games.json, no self-update
}

void test_offline_not_installed() {
    std::printf("server unreachable, nothing installed: the error, a key press, exit 1\n");
    TempFolder t;
    write(t.path / "Mewgenics.exe", "game");
    FakeIo io;
    io.net_.pages[ffb::kGamesJsonUrl] = {0, ""};   // transport failure
    CHECK(ffb::run_app(input(t), io) == 1);
    CHECK(io.errs.size() == 2);
    if (io.errs.size() == 2) {
        CHECK(io.errs[0] == "Could not reach coopmods.com, and FFB Co-op is not installed in " + t.str() +
                                " yet. Connect to the internet and start it again.");
        CHECK(io.errs[1] == "Press any key to exit.");
    }
    CHECK(io.keys == 1);
    CHECK(io.starts.empty());
}

void test_offline_launcher_missing() {
    std::printf("server unreachable, games.json kept but the launcher gone: not installed\n");
    TempFolder t;
    write(t.path / "Mewgenics.exe", "game");
    install(t, false);
    FakeIo io;
    CHECK(ffb::run_app(input(t), io) == 1);
    CHECK(io.errs.size() == 2 && io.errs[0].rfind("Could not reach coopmods.com, and FFB Co-op is not installed", 0) == 0);
    CHECK(io.keys == 1);
    CHECK(io.starts.empty());
}

void test_offline_empty_folder() {
    std::printf("server unreachable, games.json kept, no game exe in the folder: the none screen\n");
    TempFolder t;
    install(t);
    FakeIo io;
    CHECK(ffb::run_app(input(t), io) == 1);
    CHECK(has(io.errs, "Could not find a FFB modded game in " + t.str()));
    CHECK(io.keys == 1);
}

void test_invalid_games_json() {
    std::printf("games.json invalid: treated as unreachable, with its own message\n");
    TempFolder t;
    write(t.path / "Mewgenics.exe", "game");
    install(t);
    FakeIo io;
    io.net_.pages[ffb::kGamesJsonUrl] = {200, "{\"schema\":2}"};
    CHECK(ffb::run_app(input(t), io) == 0);
    CHECK(io.errs.size() == 1);
    if (io.errs.size() == 1) {
        const std::string pre = "coopmods.com sent a file this version cannot read (";
        const std::string post = ") -- starting the installed version.";
        CHECK(io.errs[0].rfind(pre, 0) == 0);
        CHECK(io.errs[0].size() > pre.size() + post.size() &&
              io.errs[0].compare(io.errs[0].size() - post.size(), post.size(), post) == 0);
    }
    CHECK(io.starts.size() == 1);

    TempFolder empty;
    write(empty.path / "Mewgenics.exe", "game");
    FakeIo io2;
    io2.net_.pages[ffb::kGamesJsonUrl] = {200, "not json"};
    CHECK(ffb::run_app(input(empty), io2) == 1);
    CHECK(io2.errs.size() == 2 && io2.errs[0].rfind("coopmods.com sent a file this version cannot read (", 0) == 0);
    CHECK(io2.errs.size() == 2 && io2.errs[0].find(", and FFB Co-op is not installed in " + empty.str()) != std::string::npos);
    CHECK(io2.keys == 1);
}

void test_manifest_unreachable_installed() {
    std::printf("games.json fine, manifest unreachable, package installed: warning, then start\n");
    TempFolder t;
    write(t.path / "Mewgenics.exe", "game");
    write(t.pkg() / kLoader, kLoaderBytes);
    FakeIo io;
    io.net_.pages[ffb::kGamesJsonUrl] = {200, games_json()};
    CHECK(ffb::run_app(input(t), io) == 0);
    CHECK(io.errs.size() == 1 && io.errs[0] == "Could not reach coopmods.com -- starting the installed version.");
    CHECK(io.starts.size() == 1);
}

void test_download_failed_not_installed() {
    std::printf("a required file fails its sha256 on a fresh install: nothing to fall back on, key press\n");
    TempFolder t;
    write(t.path / "Mewgenics.exe", "game");
    FakeIo io;
    serve_all(io);
    io.net_.pages[kBase + kDll] = {200, "tampered!"};   // same size as kDllBytes, other bytes
    CHECK(ffb::run_app(input(t), io) == 1);
    CHECK(io.errs.size() == 2 && io.errs[0].rfind("Could not update FFB Co-op (", 0) == 0);
    CHECK(io.errs.size() == 2 && io.errs[0].find(", and FFB Co-op is not installed in ") != std::string::npos);
    CHECK(io.keys == 1);
    CHECK(io.starts.empty());
    CHECK(!fs::exists(t.pkg() / kLoader));   // nothing replaced
}

void test_download_failed_installed() {
    std::printf("a required file fails its sha256 over an old install: warning, the old one starts\n");
    TempFolder t;
    write(t.path / "Mewgenics.exe", "game");
    write(t.pkg() / kLoader, "old loader");
    FakeIo io;
    serve_all(io);
    io.net_.pages[kBase + kDll] = {200, "tampered!"};
    CHECK(ffb::run_app(input(t), io) == 0);
    CHECK(io.errs.size() == 1 && io.errs[0].rfind("Could not update FFB Co-op (", 0) == 0);
    CHECK(read(t.pkg() / kLoader) == "old loader");
    CHECK(io.starts.size() == 1);
    CHECK(io.keys == 0);
}

void test_self_update_failed() {
    std::printf("a newer launcher whose download fails: one warning, the flow carries on and starts\n");
    TempFolder t;
    write(t.path / "Mewgenics.exe", "game");
    FakeIo io;
    serve_all(io);
    io.net_.pages[ffb::kGamesJsonUrl] = {200, games_json("0.2.0")};
    CHECK(ffb::run_app(input(t), io) == 0);
    CHECK(io.self_.downloads == 1);
    CHECK(io.self_.warns.size() == 1);
    CHECK(io.self_.restarts == 0);
    CHECK(io.starts.size() == 1);
    CHECK(io.keys == 0);
}

void test_self_update_restart() {
    std::printf("a newer launcher that checks out: restarted, nothing else happens, exit 0\n");
    TempFolder t;
    write(t.path / "Mewgenics.exe", "game");
    FakeIo io;
    serve_all(io);
    io.self_.download_ok = true;
    io.self_.served      = "new exe";
    io.net_.pages[ffb::kGamesJsonUrl] = {200, games_json("0.2.0", ffb::sha256_hex("new exe"), 7)};
    CHECK(ffb::run_app(input(t), io) == 0);
    CHECK(io.self_.restarts == 1);
    CHECK(io.starts.empty());
    CHECK(!fs::exists(t.pkg()));   // the restarted exe does the rest
}

void test_start_fails() {
    std::printf("the launcher will not start: an error naming it, a key press, exit 1\n");
    TempFolder t;
    write(t.path / "Mewgenics.exe", "game");
    FakeIo io;
    serve_all(io);
    io.start_ok = false;
    CHECK(ffb::run_app(input(t), io) == 1);
    CHECK(io.errs.size() == 2 && io.errs[0] == "Could not start " + (t.pkg() / kLoader).u8string() + " (error 2)");
    CHECK(io.keys == 1);
}

}  // namespace

int main() {
    std::srand((unsigned)std::time(nullptr));
    test_happy_path();
    test_already_current();
    test_none_found();
    test_two_found();
    test_offline_installed();
    test_offline_not_installed();
    test_offline_launcher_missing();
    test_offline_empty_folder();
    test_invalid_games_json();
    test_manifest_unreachable_installed();
    test_download_failed_not_installed();
    test_download_failed_installed();
    test_self_update_failed();
    test_self_update_restart();
    test_start_fails();
    return ffb_test_result();
}
