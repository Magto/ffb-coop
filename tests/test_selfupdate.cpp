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
    void note(const std::string&) override {}
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

    // --- sha256 compare: any case ---
    CHECK(is_sha256_hex(kGoodSha));
    CHECK(!is_sha256_hex(kGoodSha.substr(1)));
    CHECK(!is_sha256_hex(kGoodSha.substr(1) + "g"));
    CHECK(sha256_equal("ABCDEF", "abcdef"));
    CHECK(!sha256_equal("abcdef", "abcdee"));

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

    return ffb_test_result();
}
