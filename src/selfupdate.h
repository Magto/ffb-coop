// selfupdate.h -- FFB Co-op.exe replaces itself with a newer version from the
// server and restarts (#5, docs/SPEC.md "The flow", step 2).
//
// Ported from mewgenics-coop loader/mewcoop_update.cpp (update_check and
// replace_self_and_restart): rename the running image aside, move the new file
// into place, relaunch with the same command line. That code is MIT licensed:
//
//   Copyright (c) 2026 SanTertrust
//   Copyright (c) 2026 Martin Strålenhielm (github.com/Magto) for the changes
//   made in the Mewgenics Coop fork from 2026-09-07 on
//
//   Permission is hereby granted, free of charge, to any person obtaining a copy
//   of this software and associated documentation files (the "Software"), to
//   deal in the Software without restriction, including without limitation the
//   rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
//   sell copies of the Software, and to permit persons to whom the Software is
//   furnished to do so, subject to the following conditions:
//
//   The above copyright notice and this permission notice shall be included in
//   all copies or substantial portions of the Software.
//
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
//   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
//   IN THE SOFTWARE.
//
// The rule, as in the loader: A SELF-UPDATE NEVER COSTS A START. A failed
// download, a bad sha256 or a rename Windows refuses prints one line and the
// running exe carries on as it is. And a restart never loops: the restarted
// process is told it is the update (an environment flag), and does not check
// again whatever version it finds.
//
// The decision logic is plain C++ over the SelfUpdateIo interface, so
// tests/test_selfupdate.cpp runs it with no network and no real files. The
// Windows side (WinHTTP, BCrypt, MoveFileEx, CreateProcess) is
// src/selfupdate_win.cpp.
#pragma once

#include <cstdint>
#include <string>

namespace ffb {

// The `launcher` block of games.json. The games.json parser (#3) validates and
// fills it; this module takes it as given and re-checks only what it acts on.
struct LauncherBlock {
    std::string   version;   // "MAJOR.MINOR.PATCH"
    std::string   url;       // absolute https:// URL of the new FFB Co-op.exe
    std::string   sha256;    // 64 hex digits, any case
    std::uint64_t size = 0;  // exact byte count of the download
};

struct Version {
    int major = 0, minor = 0, patch = 0;
};

// "MAJOR.MINOR.PATCH", each part a non-negative decimal integer with no sign,
// nothing before or after (docs/SPEC.md, launcher.version). -> false otherwise.
bool parse_version(const std::string& s, Version* out);

// Part by part as numbers: 1.10.0 is newer than 1.9.0. -> -1, 0 or 1.
int compare_version(const Version& a, const Version& b);

// The version this exe was built as (src/ffb_version.h, the same numbers the
// version resource carries).
Version running_version();

// Exactly 64 hex digits, any case.
bool is_sha256_hex(const std::string& s);

// Hex strings equal without regard to case.
bool sha256_equal(const std::string& a, const std::string& b);

// Everything the update touches outside this process's memory. Paths are wide
// strings because the game folder can hold any character; URLs and hashes are
// ASCII.
class SelfUpdateIo {
public:
    virtual ~SelfUpdateIo() = default;

    // True when this process was started by a self-update (the flag restart()
    // sets for its child). Such a process never updates again.
    virtual bool is_restarted_child() = 0;

    // Full path of the running exe, e.g. C:\Games\Mewgenics\FFB Co-op.exe.
    virtual std::wstring self_path() = 0;

    // GET `url` into `dest` (created or truncated). `sha256_hex` gets the
    // lowercase sha256 of the bytes written and `got` their count. -> false on
    // any transport or HTTP failure; a partial `dest` may be left behind.
    virtual bool download(const std::string& url, const std::wstring& dest,
                          std::string* sha256_hex, std::uint64_t* got) = 0;

    // Moves `from` to `to`, replacing `to` if it exists. Must work on the
    // running image (Windows allows renaming it). -> false on failure.
    virtual bool move_replace(const std::wstring& from, const std::wstring& to) = 0;

    // Deletes `path`; missing is fine.
    virtual void remove(const std::wstring& path) = 0;

    // Starts `exe` with this process's own command line, marked as the
    // restarted child. -> false when it would not start.
    virtual bool restart(const std::wstring& exe) = 0;

    // One console line: `note` for progress, `warn` for a failure.
    virtual void note(const std::string& line) = 0;
    virtual void warn(const std::string& line) = 0;
};

enum class SelfUpdateOutcome {
    Skipped,    // this process is the restarted child; not checked again
    UpToDate,   // the server's version is the same or older
    Restart,    // the new exe is in place and started -- the caller exits 0 NOW
    Failed,     // warned; the running exe is untouched or put back. Carry on.
};

// "C:\x\FFB Co-op.exe" -> "C:\x\FFB Co-op.exe.new": where the download lands.
std::wstring staged_path(const std::wstring& self);
// "C:\x\FFB Co-op.exe" -> "C:\x\FFB Co-op.old.exe": where the running image
// is moved aside to.
std::wstring old_path(const std::wstring& self);

// The whole self-update: compare, download, verify, swap, restart.
SelfUpdateOutcome self_update(const LauncherBlock& block, const Version& running,
                              SelfUpdateIo& io);

// Deletes the .old.exe a previous self-update left behind. Call it early on
// every start: the parent may still be exiting and holding it, so the Windows
// side retries briefly, and whatever is left is swept next time.
void self_update_sweep(SelfUpdateIo& io);

// The Windows SelfUpdateIo (src/selfupdate_win.cpp). One per process.
#ifdef _WIN32
SelfUpdateIo& windows_self_update_io();
#endif

} // namespace ffb
