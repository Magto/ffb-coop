// test_app.cpp -- the start flow (#6, docs/SPEC.md "The flow", "Error screens",
// "Offline"): every screen, the happy path, the offline starts and the
// self-update outcomes, with a fake network, a fake self-update, a fake process
// start and a fake key press. The game folder is a real temporary folder with a
// space in its name, because the finder and the package update (#3, #4) work on
// real files.
//
// The screen texts asserted here are copied from docs/SPEC.md, not from a run.
// games.json and the manifest are served with a .sig made with RFC 8032's test
// key (ffb_sign_test.h), the one key the fake launcher trusts (#21).
#include "ffb_test.h"
#include "ffb_sign_test.h"
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

// A <url>.sig the test did not set is the valid signature of whatever <url>
// serves at that moment, as a correct server sends it (#21); a test that wants
// it missing or wrong sets the .sig page itself.
struct FakeNet : ffb::Net {
    std::map<std::string, std::pair<int, std::string>> pages;   // url -> status, body
    std::vector<std::string> asked;
    int get(const std::string& url, const ffb::Sink& sink) override {
        asked.push_back(url);
        auto it = pages.find(url);
        const std::string sig_ext = ".sig";
        if (it == pages.end() && url.size() > sig_ext.size() &&
            url.compare(url.size() - sig_ext.size(), sig_ext.size(), sig_ext) == 0) {
            const auto signed_page = pages.find(url.substr(0, url.size() - sig_ext.size()));
            if (signed_page == pages.end() || signed_page->second.first != 200) return 404;
            const std::string sig = ffb_test::sign(signed_page->second.second);
            sink(sig.data(), sig.size());
            return 200;
        }
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
    std::vector<std::string> urls;   // every download asked for
    bool is_restarted_child() override { return false; }
    std::wstring self_path() override { return L"C:\\Games\\FFB Co-op.exe"; }
    bool download(const std::string& url, const std::wstring&, std::string* hex, std::uint64_t* got) override {
        ++downloads;
        urls.push_back(url);
        if (!download_ok) return false;
        *hex = ffb::sha256_hex(served);
        *got = served.size();
        return true;
    }
    bool move_replace(const std::wstring&, const std::wstring&) override { return true; }
    std::vector<std::wstring> removed;
    void remove(const std::wstring& p) override { removed.push_back(p); }
    bool restart(const std::wstring&) override { ++restarts; return true; }
    void note(const std::string&) override {}
    void warn(const std::string& line) override { warns.push_back(line); }
};

struct Start { std::string exe, cmdline, workdir; };

int g_bad_waits = 0;   // summed over every FakeIo, checked once at the end

struct FakeIo : ffb::AppIo {
    ~FakeIo() override { g_bad_waits += bad_waits; }
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
    // Every key wait must come after "Press any key to exit." is on the screen:
    // a wait with any other last line counts as a bad wait.
    int bad_waits = 0;
    void wait_key() override {
        ++keys;
        if (errs.empty() || errs.back() != "Press any key to exit.") ++bad_waits;
    }
    void out(const std::string& l) override { outs.push_back(l); }
    void err(const std::string& l) override { errs.push_back(l); }
    std::FILE* package_log() override { return nullptr; }
    const std::vector<ffb::PublicKey>& trusted_keys() override { return ffb_test::test_keys(); }
    int log_ins = 0, refusals = 0;   // the dev login hooks (#26)
    void log_in() override { ++log_ins; }
    void login_refused() override { ++refusals; }
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
    fs::path dev() const { return path / "FFB Co-op dev"; }   // the dev exe's package folder (#26)
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

// What an earlier good online start leaves behind: games.json and the manifest
// kept, and the package's required files in place (both, or all but `missing`).
void install(const TempFolder& t, const std::string& missing = "") {
    write(t.pkg() / "games.json", games_json());
    write(t.pkg() / "manifest.json", manifest());
    if (missing != kLoader) write(t.pkg() / kLoader, kLoaderBytes);
    if (missing != kDll) write(t.pkg() / kDll, kDllBytes);
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
    CHECK(read(t.pkg() / "manifest.json") == manifest());   // and the required-file list
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
    CHECK(io.net_.asked.size() == 4);   // games.json, the manifest and their two .sig files, no file
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

void test_offline_file_missing() {
    const std::string not_installed = "Could not reach coopmods.com, and FFB Co-op is not installed";
    for (const std::string& gone : {kLoader, kDll}) {
        std::printf("server unreachable, a required file (%s) gone: not installed\n", gone.c_str());
        TempFolder t;
        write(t.path / "Mewgenics.exe", "game");
        install(t, gone);
        FakeIo io;
        CHECK(ffb::run_app(input(t), io) == 1);
        CHECK(io.errs.size() == 2 && io.errs[0].rfind(not_installed, 0) == 0);
        CHECK(io.keys == 1);
        CHECK(io.starts.empty());
    }
    std::printf("server unreachable, every file there but no manifest kept: not installed\n");
    TempFolder t;
    write(t.path / "Mewgenics.exe", "game");
    install(t);
    fs::remove(t.pkg() / "manifest.json");
    FakeIo io;
    CHECK(ffb::run_app(input(t), io) == 1);
    CHECK(io.errs.size() == 2 && io.errs[0].rfind(not_installed, 0) == 0);
    CHECK(io.starts.empty());
}

void test_offline_two_found() {
    std::printf("server unreachable, games.json kept, two game exes: the two-games screen\n");
    TempFolder t;
    write(t.path / "Mewgenics.exe", "game");
    write(t.path / "Other.exe", "game");
    install(t);
    FakeIo io;
    CHECK(ffb::run_app(input(t), io) == 1);
    CHECK(has(io.errs, "Found more than one FFB modded game in " + t.str() + ": Mewgenics.exe, Other.exe"));
    CHECK(io.keys == 1);
    CHECK(io.starts.empty());
}

void test_sweep() {
    std::printf("every start sweeps the .old.exe a previous self-update left behind\n");
    TempFolder t;
    FakeIo io;
    ffb::run_app(input(t), io);
    CHECK(!io.self_.removed.empty() && io.self_.removed.front() == L"C:\\Games\\FFB Co-op.old.exe");
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
    install(t);
    FakeIo io;
    io.net_.pages[ffb::kGamesJsonUrl] = {200, games_json()};
    CHECK(ffb::run_app(input(t), io) == 0);
    CHECK(io.errs.size() == 1 && io.errs[0] == "Could not reach coopmods.com -- starting the installed version.");
    CHECK(io.starts.size() == 1);
}

void test_invalid_manifest() {
    const std::string pre  = "coopmods.com sent a file this version cannot read (";
    const std::string post = ") -- starting the installed version.";
    // Breaks the manifest rules: "files" must hold at least one entry.
    const std::string bad = "{\"version\":\"76.0.0\",\"wire\":34,\"files\":[]}";

    std::printf("games.json fine, manifest invalid, package installed: the cannot-read line, then start\n");
    TempFolder t;
    write(t.path / "Mewgenics.exe", "game");
    install(t);
    FakeIo io;
    serve_all(io);
    io.net_.pages[kManifestUrl] = {200, bad};
    CHECK(ffb::run_app(input(t), io) == 0);
    CHECK(io.errs.size() == 1);
    if (io.errs.size() == 1) {
        CHECK(io.errs[0].rfind(pre, 0) == 0);
        CHECK(io.errs[0].size() > pre.size() + post.size() &&
              io.errs[0].compare(io.errs[0].size() - post.size(), post.size(), post) == 0);
    }
    CHECK(io.starts.size() == 1);
    CHECK(io.keys == 0);
    CHECK(read(t.pkg() / "manifest.json") == manifest());   // the kept one is not overwritten

    std::printf("games.json fine, manifest invalid, nothing installed: the not-installed screen\n");
    TempFolder t2;
    write(t2.path / "Mewgenics.exe", "game");
    FakeIo io2;
    serve_all(io2);
    io2.net_.pages[kManifestUrl] = {200, bad};
    CHECK(ffb::run_app(input(t2), io2) == 1);
    CHECK(io2.errs.size() == 2);
    if (io2.errs.size() == 2) {
        CHECK(io2.errs[0].rfind(pre, 0) == 0);
        CHECK(io2.errs[0].find("), and FFB Co-op is not installed in " + t2.str() +
                               " yet. Connect to the internet and start it again.") != std::string::npos);
        CHECK(io2.errs[1] == "Press any key to exit.");
    }
    CHECK(io2.keys == 1);
    CHECK(io2.starts.empty());
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

    std::printf("a download fails with only the launcher there: the fetched manifest wants %s too\n", kDll.c_str());
    TempFolder t2;
    write(t2.path / "Mewgenics.exe", "game");
    write(t2.pkg() / kLoader, "old loader");
    FakeIo io2;
    serve_all(io2);
    io2.net_.pages[kBase + kDll] = {200, "tampered!"};
    CHECK(ffb::run_app(input(t2), io2) == 1);
    CHECK(io2.errs.size() == 2 && io2.errs[0].find(", and FFB Co-op is not installed in ") != std::string::npos);
    CHECK(io2.starts.empty());
}

void test_download_failed_installed() {
    std::printf("a required file fails its sha256 over an old install: warning, the old one starts\n");
    TempFolder t;
    write(t.path / "Mewgenics.exe", "game");
    write(t.pkg() / kLoader, "old loader");
    write(t.pkg() / kDll, "old dll");   // no manifest kept: the fetched one decides
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

void test_version_switch() {
    std::printf("--version alone: the version line, exit 0, no network, nothing written\n");
    TempFolder t;
    write(t.path / "Mewgenics.exe", "game");
    FakeIo io;
    serve_all(io);
    CHECK(ffb::run_app(input(t, "--version"), io) == 0);
    CHECK(io.outs.size() == 1 && io.outs[0].rfind("FFB Co-op ", 0) == 0);
    CHECK(io.net_.asked.empty());
    CHECK(io.starts.empty());
    CHECK(io.keys == 0);
    CHECK(!fs::exists(t.pkg()));

    FakeIo io3;
    CHECK(ffb::run_app(input(t, "--version  "), io3) == 0);   // trailing blanks as typed
    CHECK(io3.net_.asked.empty());

    std::printf("--version with more arguments: not the switch, passed on like any other\n");
    FakeIo io2;
    serve_all(io2);
    CHECK(ffb::run_app(input(t, "--version -x"), io2) == 0);
    CHECK(io2.starts.size() == 1 &&
          io2.starts[0].cmdline == q(t.pkg() / kLoader) + " " + q(t.path / "Mewgenics.exe") + " --version -x");
}

// Every file under `dir`, path -> bytes: equal snapshots mean nothing in the
// game folder changed, was added or went away.
std::map<std::string, std::string> snapshot(const fs::path& dir) {
    std::map<std::string, std::string> m;
    for (const auto& e : fs::recursive_directory_iterator(dir))
        m[e.path().u8string()] = e.is_regular_file() ? read(e.path()) : std::string("<dir>");
    return m;
}

const std::string kGamesSigUrl    = std::string(ffb::kGamesJsonUrl) + ".sig";
const std::string kManifestSigUrl = kManifestUrl + ".sig";
const std::string kUnsignedPre    = "coopmods.com sent a file without a valid signature (";
const std::string kInstalledPost  = ") -- starting the installed version.";

// One way to get a signature wrong: what the server sends as the .sig, and the
// reason the launcher gives, from docs/SPEC.md "Signatures".
struct BadSig { const char* what; int status; std::string body; std::string reason; };

std::vector<BadSig> bad_sigs(const std::string& file_name, const std::string& sig_url,
                             const std::string& served, const std::string& genuine) {
    const std::string mismatch = file_name + ": the signature does not match";
    return {
        {"no .sig (404)", 404, "", (file_name == "games.json" ? "games.json is not signed: " : "the manifest is not signed: ") +
                                       sig_url + " said HTTP 404"},
        {"empty .sig", 200, "", file_name + ": the signature is not 128 hex digits"},
        {"signed by a key nobody trusts", 200,
         ffb_test::sign(served, ffb_test::kOtherSeedHex, ffb_test::kOtherPublicHex), mismatch},
        {"the genuine file's .sig on tampered bytes", 200, ffb_test::sign(genuine), mismatch},
    };
}

void test_games_json_unsigned() {
    // The attack: a games.json advertising a newer launcher whose bytes check
    // out. Signed, it restarts into the new exe (test_self_update_restart);
    // unsigned or badly signed it must not even be read.
    const std::string genuine = games_json();
    const std::string served  = games_json("0.2.0", ffb::sha256_hex("new exe"), 7);
    for (const BadSig& b : bad_sigs("games.json", kGamesSigUrl, served, genuine)) {
        std::printf("games.json %s, package installed: warning, no self-update, the installed version starts\n", b.what);
        TempFolder t;
        write(t.path / "Mewgenics.exe", "game");
        install(t);
        const auto before = snapshot(t.path);
        FakeIo io;
        serve_all(io);
        io.self_.download_ok = true;
        io.self_.served      = "new exe";
        io.net_.pages[ffb::kGamesJsonUrl] = {200, served};
        io.net_.pages[kGamesSigUrl]       = {b.status, b.body};
        CHECK(ffb::run_app(input(t), io) == 0);
        CHECK(io.errs.size() == 1);
        if (io.errs.size() == 1) CHECK(io.errs[0] == kUnsignedPre + b.reason + kInstalledPost);
        CHECK(io.self_.downloads == 0);
        CHECK(io.self_.restarts == 0);
        CHECK(io.starts.size() == 1);
        CHECK(io.keys == 0);
        CHECK(snapshot(t.path) == before);   // nothing kept from it, no package update
        CHECK(!io.net_.asked.empty() && io.net_.asked.back() == kGamesSigUrl);   // the manifest never asked
    }

    std::printf("games.json unsigned, nothing installed: the not-installed screen\n");
    TempFolder t;
    write(t.path / "Mewgenics.exe", "game");
    FakeIo io;
    serve_all(io);
    io.net_.pages[kGamesSigUrl] = {404, ""};
    CHECK(ffb::run_app(input(t), io) == 1);
    CHECK(io.errs.size() == 2);
    if (io.errs.size() == 2)
        CHECK(io.errs[0] == kUnsignedPre + "games.json is not signed: " + kGamesSigUrl + " said HTTP 404), and FFB Co-op "
                            "is not installed in " + t.str() + " yet. Connect to the internet and start it again.");
    CHECK(io.keys == 1);
    CHECK(io.starts.empty());
    CHECK(!fs::exists(t.pkg()));

    std::printf("games.json.sig gets no answer: treated as unreachable\n");
    TempFolder t2;
    write(t2.path / "Mewgenics.exe", "game");
    install(t2);
    FakeIo io2;
    serve_all(io2);
    io2.net_.pages[ffb::kGamesJsonUrl] = {200, served};
    io2.net_.pages[kGamesSigUrl]       = {0, ""};
    CHECK(ffb::run_app(input(t2), io2) == 0);
    CHECK(io2.errs.size() == 1 && io2.errs[0] == "Could not reach coopmods.com -- starting the installed version.");
    CHECK(io2.self_.downloads == 0);
}

void test_games_json_default_keys() {
    std::printf("the real exe's keys: a games.json signed with the test key is refused\n");
    struct RealKeysIo : FakeIo {
        const std::vector<ffb::PublicKey>& trusted_keys() override { return ffb::AppIo::trusted_keys(); }
    };
    TempFolder t;
    write(t.path / "Mewgenics.exe", "game");
    install(t);
    RealKeysIo io;
    serve_all(io);
    io.net_.pages[ffb::kGamesJsonUrl] = {200, games_json("0.2.0")};
    CHECK(ffb::run_app(input(t), io) == 0);
    CHECK(io.errs.size() == 1 && io.errs[0] == kUnsignedPre + "games.json: the signature does not match" + kInstalledPost);
    CHECK(io.self_.downloads == 0);
}

void test_manifest_unsigned() {
    // The attack: the manifest swaps mewcoop.dll and its sha256. Valid in every
    // other way, so only the signature can refuse it.
    const std::string evil = "evil dll";
    auto manifest_with = [](const std::string& dll) {
        auto f = [](const std::string& name, const std::string& body) {
            return "{\"name\":\"" + name + "\",\"size\":" + std::to_string(body.size()) + ",\"sha256\":\"" +
                   ffb::sha256_hex(body) + "\",\"required\":true}";
        };
        return "{\"version\":\"77.0.0\",\"wire\":34,\"files\":[" + f(kLoader, kLoaderBytes) + "," + f(kDll, dll) + "]}";
    };
    const std::string forged = manifest_with(evil);
    for (const BadSig& b : bad_sigs("the manifest", kManifestSigUrl, forged, manifest())) {
        std::printf("manifest %s, package installed: warning, no file in the game folder changes, the installed version starts\n",
                    b.what);
        TempFolder t;
        write(t.path / "Mewgenics.exe", "game");
        install(t);
        // The kept games.json differs from the one served, so a re-save would show.
        write(t.pkg() / "games.json", games_json("0.0.9"));
        const auto before = snapshot(t.path);
        FakeIo io;
        serve_all(io);
        io.net_.pages[kManifestUrl]    = {200, forged};
        io.net_.pages[kBase + kDll]    = {200, evil};
        io.net_.pages[kManifestSigUrl] = {b.status, b.body};
        CHECK(ffb::run_app(input(t), io) == 0);
        CHECK(io.errs.size() == 1);
        if (io.errs.size() == 1) CHECK(io.errs[0] == kUnsignedPre + b.reason + kInstalledPost);
        CHECK(io.starts.size() == 1);
        CHECK(io.keys == 0);
        CHECK(snapshot(t.path) == before);
        bool fetched_dll = false;
        for (const auto& u : io.net_.asked) if (u == kBase + kDll) fetched_dll = true;
        CHECK(!fetched_dll);
    }

    std::printf("manifest unsigned, nothing installed: the not-installed screen, no FFB Co-op folder made\n");
    TempFolder t;
    write(t.path / "Mewgenics.exe", "game");
    FakeIo io;
    serve_all(io);
    io.net_.pages[kManifestSigUrl] = {404, ""};
    CHECK(ffb::run_app(input(t), io) == 1);
    CHECK(io.errs.size() == 2 && io.errs[0].rfind(kUnsignedPre + "the manifest is not signed: ", 0) == 0);
    CHECK(io.keys == 1);
    CHECK(io.starts.empty());
    CHECK(!fs::exists(t.pkg()));

    std::printf("control: the same forged manifest, validly signed, is installed -- only the signature refused it\n");
    TempFolder t2;
    write(t2.path / "Mewgenics.exe", "game");
    install(t2);
    FakeIo io2;
    serve_all(io2);
    io2.net_.pages[kManifestUrl] = {200, forged};
    io2.net_.pages[kBase + kDll] = {200, evil};
    CHECK(ffb::run_app(input(t2), io2) == 0);
    CHECK(read(t2.pkg() / kDll) == evil);
}

// --- the dev channel (#26) ------------------------------------------------------
//
// The same run_app with the dev channel in AppInput, as FFB Co-op - dev.exe runs
// it. The URLs and the folder name are written out from docs/SPEC.md "Dev
// channel", not read from src/channel.cpp; test_channel checks the table
// against the same strings.

const std::string kDevGamesUrl    = "https://coopmods.com/dev/games.json";
const std::string kDevManifestUrl = "https://coopmods.com/dev/mewgenics/manifest.json";
const std::string kDevBase        = "https://coopmods.com/dev/mewgenics/";
const std::string kDevSelfUrl     = "https://coopmods.com/dev/launcher/FFB%20Co-op%20-%20dev.exe";
const std::string kDevPrefix      = "https://coopmods.com/dev/";

std::string dev_games_json(const std::string& version = "1.0.1", const std::string& sha = kSha0,
                           std::uint64_t size = 1) {
    return "{\"schema\":1,\"launcher\":{\"version\":\"" + version + "\",\"url\":\"" + kDevSelfUrl +
           "\",\"sha256\":\"" + sha + "\",\"size\":" + std::to_string(size) + "},\"games\":[" +
           "{\"id\":\"mewgenics\",\"name\":\"Mewgenics\",\"exe\":\"Mewgenics.exe\",\"steam_appid\":0,"
           "\"manifest\":\"" + kDevManifestUrl + "\",\"launcher\":\"" + kLoader + "\"}]}";
}

const std::string kDevDllBytes = "dev dll bytes";

std::string dev_manifest() {
    auto f = [](const std::string& name, const std::string& body) {
        return "{\"name\":\"" + name + "\",\"size\":" + std::to_string(body.size()) + ",\"sha256\":\"" +
               ffb::sha256_hex(body) + "\",\"required\":true}";
    };
    return "{\"version\":\"77.0.0\",\"files\":[" + f(kLoader, kLoaderBytes) + "," + f(kDll, kDevDllBytes) + "]}";
}

void serve_dev(FakeIo& io) {
    io.net_.pages[kDevGamesUrl]       = {200, dev_games_json()};
    io.net_.pages[kDevManifestUrl]    = {200, dev_manifest()};
    io.net_.pages[kDevBase + kLoader] = {200, kLoaderBytes};
    io.net_.pages[kDevBase + kDll]    = {200, kDevDllBytes};
}

ffb::AppInput dev_input(const TempFolder& t, const std::string& tail = "") {
    ffb::AppInput in = input(t, tail);
    in.channel = &ffb::dev_channel();
    in.running = {1, 0, 1};
    return in;
}

void install_dev(const TempFolder& t) {
    write(t.dev() / "games.json", dev_games_json());
    write(t.dev() / "manifest.json", dev_manifest());
    write(t.dev() / kLoader, kLoaderBytes);
    write(t.dev() / kDll, kDevDllBytes);
}

bool all_under(const std::vector<std::string>& urls, const std::string& prefix) {
    for (const auto& u : urls) if (u.compare(0, prefix.size(), prefix) != 0) return false;
    return true;
}

bool any_has(const std::vector<std::string>& urls, const std::string& part) {
    for (const auto& u : urls) if (u.find(part) != std::string::npos) return true;
    return false;
}

void test_dev_beside_public() {
    std::printf("dev exe beside an installed FFB Co-op\\: installs into FFB Co-op dev\\, FFB Co-op\\ untouched\n");
    TempFolder t;
    write(t.path / "Mewgenics.exe", "game");
    install(t);
    const auto public_before = snapshot(t.pkg());
    FakeIo io;
    serve_all(io);   // the public channel is live too; the dev exe must not read it
    serve_dev(io);
    CHECK(ffb::run_app(dev_input(t, "-x"), io) == 0);
    CHECK(io.log_ins == 1);
    CHECK(io.refusals == 0);
    CHECK(read(t.dev() / kLoader) == kLoaderBytes);
    CHECK(read(t.dev() / kDll) == kDevDllBytes);
    CHECK(read(t.dev() / "games.json") == dev_games_json());
    CHECK(read(t.dev() / "manifest.json") == dev_manifest());
    CHECK(snapshot(t.pkg()) == public_before);
    CHECK(read(t.pkg() / kDll) == kDllBytes);
    CHECK(!io.net_.asked.empty() && all_under(io.net_.asked, kDevPrefix));
    CHECK(io.starts.size() == 1);
    if (io.starts.size() == 1) {
        CHECK(io.starts[0].exe == (t.dev() / kLoader).u8string());
        CHECK(io.starts[0].workdir == t.dev().u8string());
        CHECK(io.starts[0].cmdline == q(t.dev() / kLoader) + " " + q(t.path / "Mewgenics.exe") + " -x");
    }
    CHECK(io.errs.empty());
    CHECK(io.outs.size() >= 1 && io.outs[0].rfind("FFB Co-op", 0) == 0);
}

void test_public_never_dev() {
    std::printf("public exe with a dev install beside it: never asks the dev channel, never logs in, dev folder untouched\n");
    TempFolder t;
    write(t.path / "Mewgenics.exe", "game");
    install_dev(t);
    const auto dev_before = snapshot(t.dev());
    FakeIo io;
    serve_all(io);
    serve_dev(io);
    CHECK(ffb::run_app(input(t), io) == 0);
    CHECK(io.log_ins == 0);
    CHECK(!io.net_.asked.empty() && !any_has(io.net_.asked, "/dev/"));
    CHECK(io.starts.size() == 1 && io.starts[0].exe == (t.pkg() / kLoader).u8string());
    CHECK(snapshot(t.dev()) == dev_before);

    std::printf("public exe offline with only a dev install: not installed (it never borrows FFB Co-op dev\\)\n");
    TempFolder t2;
    write(t2.path / "Mewgenics.exe", "game");
    install_dev(t2);
    FakeIo io2;   // every URL a 404
    CHECK(ffb::run_app(input(t2), io2) == 1);
    CHECK(io2.starts.empty() && io2.keys == 1);
    CHECK(!fs::exists(t2.pkg()));
}

void test_dev_offline() {
    std::printf("dev exe offline with only FFB Co-op\\ installed: not installed (it never borrows FFB Co-op\\)\n");
    TempFolder t;
    write(t.path / "Mewgenics.exe", "game");
    install(t);
    const auto public_before = snapshot(t.pkg());
    FakeIo io;   // every URL a 404
    CHECK(ffb::run_app(dev_input(t), io) == 1);
    CHECK(io.errs.size() == 2);
    if (io.errs.size() == 2)
        CHECK(io.errs[0] == "Could not reach coopmods.com, and FFB Co-op is not installed in " + t.str() +
                                " yet. Connect to the internet and start it again.");
    CHECK(io.starts.empty());
    CHECK(snapshot(t.pkg()) == public_before);

    std::printf("dev exe offline with FFB Co-op dev\\ installed: one warning, the installed dev package starts\n");
    install_dev(t);
    FakeIo io2;
    CHECK(ffb::run_app(dev_input(t), io2) == 0);
    CHECK(io2.errs.size() == 1 && io2.errs[0] == "Could not reach coopmods.com -- starting the installed version.");
    CHECK(io2.starts.size() == 1 && io2.starts[0].exe == (t.dev() / kLoader).u8string());
    CHECK(io2.self_.downloads == 0);
    CHECK(snapshot(t.pkg()) == public_before);
}

void test_dev_login_refused() {
    std::printf("dev games.json answers 401: the login is forgotten, the installed dev package starts\n");
    TempFolder t;
    write(t.path / "Mewgenics.exe", "game");
    install_dev(t);
    FakeIo io;
    serve_dev(io);
    io.net_.pages[kDevGamesUrl] = {401, ""};
    CHECK(ffb::run_app(dev_input(t), io) == 0);
    CHECK(io.refusals == 1);
    CHECK(io.errs.size() == 1 &&
          io.errs[0] == "coopmods.com refused your dev login (HTTP 401) -- it is forgotten, and the next start "
                        "asks again -- starting the installed version.");
    CHECK(io.self_.downloads == 0);
    CHECK(io.starts.size() == 1);

    std::printf("the public exe never treats a 401 as a login problem\n");
    TempFolder t2;
    write(t2.path / "Mewgenics.exe", "game");
    install(t2);
    FakeIo io2;
    io2.net_.pages[ffb::kGamesJsonUrl] = {401, ""};
    CHECK(ffb::run_app(input(t2), io2) == 0);
    CHECK(io2.refusals == 0 && io2.log_ins == 0);
    CHECK(io2.errs.size() == 1 && io2.errs[0] == "Could not reach coopmods.com -- starting the installed version.");
}

void test_dev_self_update() {
    std::printf("dev exe: a newer dev launcher that checks out is fetched from the dev URL and restarts\n");
    {
        TempFolder t;
        write(t.path / "Mewgenics.exe", "game");
        FakeIo io;
        serve_dev(io);
        io.self_.download_ok = true;
        io.self_.served      = "new dev exe";
        io.net_.pages[kDevGamesUrl] = {200, dev_games_json("1.0.2", ffb::sha256_hex("new dev exe"), 11)};
        CHECK(ffb::run_app(dev_input(t), io) == 0);
        CHECK(io.self_.restarts == 1);
        CHECK(io.self_.urls.size() == 1 && io.self_.urls[0] == kDevSelfUrl);
        CHECK(io.starts.empty());
    }
    std::printf("dev exe: the same or a lower dev version is not downloaded\n");
    for (const char* ver : {"1.0.1", "1.0.0"}) {
        TempFolder t;
        write(t.path / "Mewgenics.exe", "game");
        FakeIo io;
        serve_dev(io);
        io.self_.download_ok = true;
        io.net_.pages[kDevGamesUrl] = {200, dev_games_json(ver)};
        CHECK(ffb::run_app(dev_input(t), io) == 0);
        CHECK(io.self_.downloads == 0);
        CHECK(io.starts.size() == 1);
    }
    std::printf("dev exe: a dev games.json without a valid signature is refused, no self-update\n");
    for (const BadSig& b : bad_sigs("games.json", kDevGamesUrl + ".sig",
                                    dev_games_json("1.0.2", ffb::sha256_hex("new dev exe"), 11), dev_games_json())) {
        TempFolder t;
        write(t.path / "Mewgenics.exe", "game");
        install_dev(t);
        FakeIo io;
        serve_dev(io);
        io.self_.download_ok = true;
        io.self_.served      = "new dev exe";
        io.net_.pages[kDevGamesUrl]          = {200, dev_games_json("1.0.2", ffb::sha256_hex("new dev exe"), 11)};
        io.net_.pages[kDevGamesUrl + ".sig"] = {b.status, b.body};
        CHECK(ffb::run_app(dev_input(t), io) == 0);
        CHECK(io.errs.size() == 1 && io.errs[0] == kUnsignedPre + b.reason + kInstalledPost);
        CHECK(io.self_.downloads == 0);
        CHECK(io.starts.size() == 1);
    }
}

void test_dev_none_found() {
    std::printf("dev exe, no game: the screen names FFB Co-op - dev.exe\n");
    TempFolder t;
    FakeIo io;
    serve_dev(io);
    CHECK(ffb::run_app(dev_input(t), io) == 1);
    CHECK(io.errs.size() == 4);
    if (io.errs.size() == 4)
        CHECK(io.errs[2] == "Put FFB Co-op - dev.exe next to the game's exe and start it again.");
    CHECK(!fs::exists(t.dev()) && !fs::exists(t.pkg()));
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
    test_offline_file_missing();
    test_offline_empty_folder();
    test_offline_two_found();
    test_sweep();
    test_invalid_games_json();
    test_manifest_unreachable_installed();
    test_invalid_manifest();
    test_download_failed_not_installed();
    test_download_failed_installed();
    test_self_update_failed();
    test_self_update_restart();
    test_start_fails();
    test_version_switch();
    test_games_json_unsigned();
    test_games_json_default_keys();
    test_manifest_unsigned();
    test_dev_beside_public();
    test_public_never_dev();
    test_dev_offline();
    test_dev_login_refused();
    test_dev_self_update();
    test_dev_none_found();
    std::printf("every key wait came after \"Press any key to exit.\"\n");
    CHECK(g_bad_waits == 0);
    return ffb_test_result();
}
