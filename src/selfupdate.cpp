// selfupdate.cpp -- the self-update decision and swap, over SelfUpdateIo.
// See selfupdate.h for what it is, the MIT notice and the credit; this file is
// how. Nothing here touches the network or the disk directly, so
// tests/test_selfupdate.cpp runs every branch with a fake.
//
// The order, as in mewgenics-coop's replace_self_and_restart:
//
//   1. A restarted child stops here. That one check is what makes a loop
//      impossible, whatever version the child finds itself to be.
//   2. The server's version must be strictly newer than the running one.
//   3. Download to "FFB Co-op.exe.new"; the size and sha256 of the bytes that
//      arrived must match the launcher block. Anything else deletes the .new
//      and replaces nothing.
//   4. Move the running image aside to "FFB Co-op.old.exe" (Windows allows
//      renaming a running exe, not overwriting it), move the .new into its
//      place, start it with the same command line. Every failure puts the old
//      exe back.
#include "selfupdate.h"

#include "ffb_version.h"

#include <cctype>
#include <cwctype>
#include <string>

namespace ffb {

namespace {

// One part of a version: 1+ decimal digits, no sign, fits an int.
bool parse_part(const std::string& s, size_t* pos, int* out) {
    size_t i = *pos;
    long long v = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        v = v * 10 + (s[i] - '0');
        if (v > 0x7fffffff) return false;
        ++i;
    }
    if (i == *pos) return false;
    *out = (int)v;
    *pos = i;
    return true;
}

// Private on purpose: #4's src/net.h (PR #17) exports ffb::is_sha256_hex and
// ffb::same_sha256, and two exported definitions of one name would not link.
// Once #17 is merged these two give way to net.h's.
bool is_sha256_hex(const std::string& s) {
    if (s.size() != 64) return false;
    for (char c : s)
        if (!std::isxdigit((unsigned char)c)) return false;
    return true;
}

bool sha256_equal(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}

std::string version_str(const Version& v) {
    return std::to_string(v.major) + "." + std::to_string(v.minor) + "." + std::to_string(v.patch);
}

} // namespace

bool parse_version(const std::string& s, Version* out) {
    Version v;
    size_t pos = 0;
    if (!parse_part(s, &pos, &v.major)) return false;
    if (pos >= s.size() || s[pos] != '.') return false;
    ++pos;
    if (!parse_part(s, &pos, &v.minor)) return false;
    if (pos >= s.size() || s[pos] != '.') return false;
    ++pos;
    if (!parse_part(s, &pos, &v.patch)) return false;
    if (pos != s.size()) return false;
    *out = v;
    return true;
}

int compare_version(const Version& a, const Version& b) {
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    return 0;
}

Version running_version() {
    Version v;
    v.major = FFB_VERSION_MAJOR;
    v.minor = FFB_VERSION_MINOR;
    v.patch = FFB_VERSION_PATCH;
    return v;
}

std::wstring staged_path(const std::wstring& self) {
    return self + L".new";
}

std::wstring old_path(const std::wstring& self) {
    // "FFB Co-op.exe" -> "FFB Co-op.old.exe"; the space stays where it is.
    const std::wstring ext = L".exe";
    if (self.size() > ext.size()) {
        std::wstring tail = self.substr(self.size() - ext.size());
        for (auto& c : tail) c = (wchar_t)std::towlower(c);
        if (tail == ext) return self.substr(0, self.size() - ext.size()) + L".old.exe";
    }
    return self + L".old";
}

SelfUpdateOutcome self_update(const LauncherBlock& block, const Version& running,
                              SelfUpdateIo& io) {
    // --- 1. never twice in a row ---
    if (io.is_restarted_child()) return SelfUpdateOutcome::Skipped;

    // --- 2. newer on the server is the only thing that starts a download ---
    Version remote;
    if (!parse_version(block.version, &remote)) {
        io.warn("self-update: the server's launcher version \"" + block.version +
                "\" is not MAJOR.MINOR.PATCH -- carrying on as " + version_str(running));
        return SelfUpdateOutcome::Failed;
    }
    if (compare_version(remote, running) <= 0) return SelfUpdateOutcome::UpToDate;

    if (!is_sha256_hex(block.sha256) || block.size == 0 ||
        block.url.compare(0, 8, "https://") != 0) {
        io.warn("self-update: the server's launcher block is incomplete -- carrying on as " +
                version_str(running));
        return SelfUpdateOutcome::Failed;
    }

    io.note("self-update: " + version_str(remote) + " is out, this is " + version_str(running) +
            " -- downloading");

    // --- 3. download and verify; nothing is touched until this passes ---
    const std::wstring self   = io.self_path();
    const std::wstring staged = staged_path(self);
    const std::wstring old    = old_path(self);

    std::string   hex;
    std::uint64_t got = 0;
    if (!io.download(block.url, staged, &hex, &got)) {
        io.remove(staged);
        io.warn("self-update: the download failed -- carrying on as " + version_str(running));
        return SelfUpdateOutcome::Failed;
    }
    if (got != block.size) {
        io.remove(staged);
        io.warn("self-update: the download is " + std::to_string(got) + " bytes, games.json says " +
                std::to_string(block.size) + " -- discarded, carrying on as " + version_str(running));
        return SelfUpdateOutcome::Failed;
    }
    if (!sha256_equal(hex, block.sha256)) {
        io.remove(staged);
        io.warn("self-update: the download does not match its sha256 -- discarded, carrying on as " +
                version_str(running));
        return SelfUpdateOutcome::Failed;
    }

    // --- 4. swap and restart; every failure puts the running exe back ---
    io.remove(old);   // a leftover from an earlier update would block the rename
    if (!io.move_replace(self, old)) {
        io.remove(staged);
        io.warn("self-update: could not move the running exe aside -- carrying on as " +
                version_str(running));
        return SelfUpdateOutcome::Failed;
    }
    if (!io.move_replace(staged, self)) {
        io.move_replace(old, self);
        io.remove(staged);
        io.warn("self-update: could not put the new exe in place -- carrying on as " +
                version_str(running));
        return SelfUpdateOutcome::Failed;
    }
    if (!io.restart(self)) {
        io.move_replace(old, self);
        io.warn("self-update: the new exe would not start -- the old one is back, carrying on as " +
                version_str(running));
        return SelfUpdateOutcome::Failed;
    }
    io.note("self-update: updated to " + version_str(remote) + " -- restarting");
    return SelfUpdateOutcome::Restart;
}

void self_update_sweep(SelfUpdateIo& io) {
    io.remove(old_path(io.self_path()));
}

} // namespace ffb
