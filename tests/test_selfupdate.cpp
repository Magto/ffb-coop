// test_selfupdate.cpp -- the self-update's version compare and decision logic
// (#5), with no network and no real files: FakeIo stands in for the Windows side
// (download, move, delete, restart) with an in-memory folder and a scripted
// server. self_update itself is the real code.
//
// Hashes below are literals the fake server "sends", not values computed by the
// code under test: the check is that self_update compares them, not that it can
// hash.
#include "selfupdate.h"
#include "ffb_test.h"

#include <cctype>
#include <map>
#include <string>
#include <vector>

using namespace ffb;

namespace {

const std::wstring kSelf = L"C:\\Games\\Mewgenics\\FFB Co-op.exe";
const std::wstring kNew  = L"C:\\Games\\Mewgenics\\FFB Co-op.exe.new";
const std::wstring kOld  = L"C:\\Games\\Mewgenics\\FFB Co-op.old.exe";

const std::string kGoodSha = "9f2c4e1a7b3d5f60812a4c6e8b0d2f4a6c8e0b2d4f6a8c0e2b4d6f8a0c2e4b6d";
const std::string kOtherSha = "0000000000000000000000000000000000000000000000000000000000000000";
const std::string kNewBytes = "NEW-EXE-BYTES";   // 13 bytes
const std::string kOldBytes = "OLD-EXE-BYTES-RUNNING";

struct FakeIo : SelfUpdateIo {
    // the folder
    std::map<std::wstring, std::string> files;
    // the server
    bool          download_ok  = true;
    std::string   server_bytes = kNewBytes;
    std::string   server_sha   = kGoodSha;
    // failures to inject
    bool restarted_child = false;
    int  fail_move_no    = 0;   // 1-based index of the move_replace call that fails
    bool restart_ok      = true;
    // what happened
    int  downloads = 0, moves = 0;
    std::vector<std::wstring> restarts;
    std::vector<std::string>  warnings;
    std::vector<std::string>  notes;

    FakeIo() { files[kSelf] = kOldBytes; }

    bool is_restarted_child() override { return restarted_child; }
    std::wstring self_path() override { return kSelf; }
    bool download(const std::string&, const std::wstring& dest, std::string* hex,
                  std::uint64_t* got) override {
        ++downloads;
        if (!download_ok) { files[dest] = "partial"; return false; }
        files[dest] = server_bytes;
        *hex = server_sha;
        *got = server_bytes.size();
        return true;
    }
    bool move_replace(const std::wstring& from, const std::wstring& to) override {
        ++moves;
        if (moves == fail_move_no) return false;
        auto it = files.find(from);
        if (it == files.end()) return false;
        files[to] = it->second;
        files.erase(from);
        return true;
    }
    void remove(const std::wstring& path) override { files.erase(path); }
    bool restart(const std::wstring& exe) override {
        restarts.push_back(exe);
        return restart_ok;
    }
    void note(const std::string& line) override { notes.push_back(line); }
    void warn(const std::string& line) override { warnings.push_back(line); }

    bool has(const std::wstring& p) const { return files.count(p) != 0; }
    std::string at(const std::wstring& p) const {
        auto it = files.find(p);
        return it == files.end() ? std::string() : it->second;
    }
};

LauncherBlock block(const std::string& version) {
    LauncherBlock b;
    b.version = version;
    b.url     = "https://coopmods.com/launcher/FFB%20Co-op.exe";
    b.sha256  = kGoodSha;
    b.size    = kNewBytes.size();
    return b;
}

Version v(int a, int b, int c) { Version x; x.major = a; x.minor = b; x.patch = c; return x; }

bool parses(const std::string& s) { Version x; return parse_version(s, &x); }

int cmp(const std::string& a, const std::string& b) {
    Version x, y;
    parse_version(a, &x);
    parse_version(b, &y);
    return compare_version(x, y);
}

// The state after any failure: the running exe is untouched, nothing staged,
// nothing restarted, and the player was told.
bool untouched(const FakeIo& io) {
    return io.at(kSelf) == kOldBytes && !io.has(kNew) && io.restarts.empty() &&
           !io.warnings.empty();
}

// The dev exe (#26): the same self_update, from the same folder as the public
// exe, under its own name and from the dev channel's URL (docs/SPEC.md "Dev
// channel"). Written out here, not read from src/channel.cpp.
const std::wstring kDevSelf = L"C:\\Games\\Mewgenics\\FFB Co-op - dev.exe";
const std::wstring kDevNew  = L"C:\\Games\\Mewgenics\\FFB Co-op - dev.exe.new";
const std::wstring kDevOld  = L"C:\\Games\\Mewgenics\\FFB Co-op - dev.old.exe";
const std::string  kDevUrl  = "https://coopmods.com/dev/launcher/FFB%20Co-op%20-%20dev.exe";

// The public exe sits beside the dev one and must come through every dev
// update byte for byte.
const std::string kPublicBytes = "PUBLIC-EXE-BYTES";

struct DevIo : FakeIo {
    std::vector<std::string> urls;
    DevIo() { files[kDevSelf] = kOldBytes; files[kSelf] = kPublicBytes; }
    std::wstring self_path() override { return kDevSelf; }
    bool download(const std::string& url, const std::wstring& dest, std::string* hex,
                  std::uint64_t* got) override {
        urls.push_back(url);
        return FakeIo::download(url, dest, hex, got);
    }
};

LauncherBlock dev_block(const std::string& version) {
    LauncherBlock b = block(version);
    b.url = kDevUrl;
    return b;
}

bool dev_untouched(const DevIo& io) {
    return io.at(kDevSelf) == kOldBytes && !io.has(kDevNew) && io.restarts.empty() &&
           !io.warnings.empty() && io.at(kSelf) == kPublicBytes;
}

} // namespace

int main() {
    // --- version parse: MAJOR.MINOR.PATCH only ---
    { Version x; CHECK(parse_version("1.2.3", &x) && x.major == 1 && x.minor == 2 && x.patch == 3); }
    CHECK(parses("0.0.0"));
    CHECK(parses("10.20.30"));
    CHECK(!parses(""));
    CHECK(!parses("1"));
    CHECK(!parses("1.0"));
    CHECK(!parses("1.0.0.0"));
    CHECK(!parses("v1.0.0"));
    CHECK(!parses("-1.0.0"));
    CHECK(!parses("+1.0.0"));
    CHECK(!parses(" 1.0.0"));
    CHECK(!parses("1.0.0 "));
    CHECK(!parses("1..0"));
    CHECK(!parses("1.0.x"));
    CHECK(!parses("99999999999.0.0"));

    // --- version compare: part by part, as numbers ---
    CHECK(cmp("1.10.0", "1.9.0") == 1);
    CHECK(cmp("1.9.0", "1.10.0") == -1);
    CHECK(cmp("2.0.0", "1.99.99") == 1);
    CHECK(cmp("1.0.1", "1.0.0") == 1);
    CHECK(cmp("1.0.0", "1.0.0") == 0);
    CHECK(cmp("0.1.0", "0.0.9") == 1);

    // --- the file names, space and all ---
    CHECK(staged_path(kSelf) == kNew);
    CHECK(old_path(kSelf) == kOld);
    CHECK(old_path(L"C:\\x\\FFB Co-op.EXE") == L"C:\\x\\FFB Co-op.old.exe");

    // --- a newer version: downloaded, checked, swapped, restarted ---
    {
        FakeIo io;
        io.files[kOld] = "LEFTOVER";   // an earlier update's .old.exe must not block the rename
        CHECK(self_update(block("0.2.0"), v(0, 1, 0), io) == SelfUpdateOutcome::Restart);
        CHECK(io.at(kSelf) == kNewBytes);
        CHECK(io.at(kOld) == kOldBytes);
        CHECK(!io.has(kNew));
        CHECK(io.restarts.size() == 1 && io.restarts[0] == kSelf);
        CHECK(io.warnings.empty());
    }
    // v1 reaches the launcher in the field (#24): 0.1.0, live on coopmods.com
    // since 2026-09-25, reads launcher.version "1.0.0" from games.json and takes
    // it. parse_version and compare_version are unchanged since 0.1.0 was built,
    // so this is the field launcher's own decision.
    {
        CHECK(parses("1.0.0"));
        CHECK(cmp("1.0.0", "0.1.0") == 1);
        FakeIo io;
        CHECK(self_update(block("1.0.0"), v(0, 1, 0), io) == SelfUpdateOutcome::Restart);
        CHECK(io.downloads == 1 && io.at(kSelf) == kNewBytes);
        CHECK(io.restarts.size() == 1 && io.warnings.empty());
        // the line #24's Log check names, first of the update's notes
        const std::string said = "self-update: 1.0.0 is out, this is 0.1.0";
        CHECK(!io.notes.empty() && io.notes[0].compare(0, said.size(), said) == 0);
    }
    // and every later release is newer than the one before: v2 over v1, v10 over v9
    CHECK(cmp("2.0.0", "1.0.0") == 1);
    CHECK(cmp("10.0.0", "9.0.0") == 1);
    // numbers, not text: 1.10.0 is newer than 1.9.0
    {
        FakeIo io;
        CHECK(self_update(block("1.10.0"), v(1, 9, 0), io) == SelfUpdateOutcome::Restart);
    }
    // the block's sha256 in upper case still matches
    {
        FakeIo io;
        LauncherBlock b = block("0.2.0");
        for (auto& c : b.sha256) c = (char)toupper((unsigned char)c);
        CHECK(self_update(b, v(0, 1, 0), io) == SelfUpdateOutcome::Restart);
    }

    // --- the same or an older version does nothing ---
    {
        FakeIo io;
        CHECK(self_update(block("0.1.0"), v(0, 1, 0), io) == SelfUpdateOutcome::UpToDate);
        CHECK(io.downloads == 0 && io.moves == 0 && io.restarts.empty() && io.at(kSelf) == kOldBytes);
    }
    {
        FakeIo io;
        CHECK(self_update(block("0.0.9"), v(0, 1, 0), io) == SelfUpdateOutcome::UpToDate);
        CHECK(io.downloads == 0 && io.moves == 0 && io.restarts.empty());
    }

    // --- a restart never loops: the restarted child does not check again ---
    {
        FakeIo io;
        io.restarted_child = true;
        CHECK(self_update(block("9.0.0"), v(0, 1, 0), io) == SelfUpdateOutcome::Skipped);
        CHECK(io.downloads == 0 && io.moves == 0 && io.restarts.empty());
    }

    // --- a bad download leaves the running exe in place and carries on ---
    {   // bad sha256
        FakeIo io;
        io.server_sha = kOtherSha;
        CHECK(self_update(block("0.2.0"), v(0, 1, 0), io) == SelfUpdateOutcome::Failed);
        CHECK(untouched(io));
        CHECK(io.moves == 0);
    }
    {   // wrong size
        FakeIo io;
        io.server_bytes = kNewBytes + "X";
        CHECK(self_update(block("0.2.0"), v(0, 1, 0), io) == SelfUpdateOutcome::Failed);
        CHECK(untouched(io));
        CHECK(io.moves == 0);
    }
    {   // the download itself fails, leaving a partial file
        FakeIo io;
        io.download_ok = false;
        CHECK(self_update(block("0.2.0"), v(0, 1, 0), io) == SelfUpdateOutcome::Failed);
        CHECK(untouched(io));
        CHECK(io.moves == 0);
    }
    {   // a block this module cannot act on: nothing is fetched
        FakeIo io;
        CHECK(self_update(block("0.2"), v(0, 1, 0), io) == SelfUpdateOutcome::Failed);
        CHECK(io.downloads == 0 && !io.warnings.empty());
        LauncherBlock b = block("0.2.0");
        b.sha256 = "abc";
        FakeIo io2;
        CHECK(self_update(b, v(0, 1, 0), io2) == SelfUpdateOutcome::Failed);
        CHECK(io2.downloads == 0);
        b = block("0.2.0");
        b.sha256 = kGoodSha.substr(1) + "g";   // 64 characters, one not hex
        FakeIo io4;
        CHECK(self_update(b, v(0, 1, 0), io4) == SelfUpdateOutcome::Failed);
        CHECK(io4.downloads == 0);
        b = block("0.2.0");
        b.url = "http://coopmods.com/launcher/FFB%20Co-op.exe";
        FakeIo io3;
        CHECK(self_update(b, v(0, 1, 0), io3) == SelfUpdateOutcome::Failed);
        CHECK(io3.downloads == 0);
    }

    // --- a swap that fails puts the running exe back ---
    {   // cannot move the running exe aside
        FakeIo io;
        io.fail_move_no = 1;
        CHECK(self_update(block("0.2.0"), v(0, 1, 0), io) == SelfUpdateOutcome::Failed);
        CHECK(untouched(io));
    }
    {   // cannot move the new exe into place
        FakeIo io;
        io.fail_move_no = 2;
        CHECK(self_update(block("0.2.0"), v(0, 1, 0), io) == SelfUpdateOutcome::Failed);
        CHECK(untouched(io));
    }
    {   // the new exe will not start
        FakeIo io;
        io.restart_ok = false;
        CHECK(self_update(block("0.2.0"), v(0, 1, 0), io) == SelfUpdateOutcome::Failed);
        CHECK(io.at(kSelf) == kOldBytes && !io.has(kNew) && !io.warnings.empty());
        CHECK(io.restarts.size() == 1);
    }

    // --- the sweep deletes the .old.exe a previous update left ---
    {
        FakeIo io;
        io.files[kOld] = kOldBytes;
        self_update_sweep(io);
        CHECK(!io.has(kOld) && io.at(kSelf) == kOldBytes);
    }

    // --- the dev exe (#26): the same rules, its own name, the dev URL ---
    CHECK(staged_path(kDevSelf) == kDevNew);
    CHECK(old_path(kDevSelf) == kDevOld);
    {   // a newer dev build: fetched from the dev URL, swapped under the dev name, restarted
        DevIo io;
        CHECK(self_update(dev_block("1.0.2"), v(1, 0, 1), io) == SelfUpdateOutcome::Restart);
        CHECK(io.urls.size() == 1 && io.urls[0] == kDevUrl);
        CHECK(io.at(kDevSelf) == kNewBytes);
        CHECK(io.at(kDevOld) == kOldBytes);
        CHECK(!io.has(kDevNew));
        CHECK(io.restarts.size() == 1 && io.restarts[0] == kDevSelf);
        CHECK(io.at(kSelf) == kPublicBytes && !io.has(kNew) && !io.has(kOld));
    }
    {   // never to the same or a lower version
        DevIo io;
        CHECK(self_update(dev_block("1.0.1"), v(1, 0, 1), io) == SelfUpdateOutcome::UpToDate);
        CHECK(self_update(dev_block("1.0.0"), v(1, 0, 1), io) == SelfUpdateOutcome::UpToDate);
        CHECK(io.downloads == 0 && io.moves == 0 && io.at(kDevSelf) == kOldBytes);
    }
    {   // the wrong sha256: the .new is discarded, nothing replaced
        DevIo io;
        io.server_sha = kOtherSha;
        CHECK(self_update(dev_block("1.0.2"), v(1, 0, 1), io) == SelfUpdateOutcome::Failed);
        CHECK(dev_untouched(io));
    }
    {   // the wrong size
        DevIo io;
        io.server_bytes = "SHORT";
        CHECK(self_update(dev_block("1.0.2"), v(1, 0, 1), io) == SelfUpdateOutcome::Failed);
        CHECK(dev_untouched(io));
    }
    {   // the download fails (offline, or the dev login refused): carry on as installed
        DevIo io;
        io.download_ok = false;
        CHECK(self_update(dev_block("1.0.2"), v(1, 0, 1), io) == SelfUpdateOutcome::Failed);
        CHECK(dev_untouched(io));
    }
    {   // the new exe cannot be put in place: the running dev exe is put back
        DevIo io;
        io.fail_move_no = 2;
        CHECK(self_update(dev_block("1.0.2"), v(1, 0, 1), io) == SelfUpdateOutcome::Failed);
        CHECK(dev_untouched(io));
    }
    {   // the restarted dev child never checks again
        DevIo io;
        io.restarted_child = true;
        CHECK(self_update(dev_block("1.0.2"), v(1, 0, 1), io) == SelfUpdateOutcome::Skipped);
        CHECK(io.downloads == 0);
    }
    {   // the sweep removes the dev .old.exe and leaves the public exe's alone
        DevIo io;
        io.files[kDevOld] = kOldBytes;
        io.files[kOld]    = "PUBLIC-OLD";
        self_update_sweep(io);
        CHECK(!io.has(kDevOld) && io.at(kOld) == "PUBLIC-OLD");
    }

    return ffb_test_result();
}
