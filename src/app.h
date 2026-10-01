// app.h -- the start flow of FFB Co-op.exe (#6, docs/SPEC.md "The flow"):
// fetch games.json, self-update, find the game, update the package, start the
// package launcher and exit -- or one of the error screens, or the offline start.
//
// The flow is a plain function over AppIo, so tests/test_app.cpp runs every
// screen with a fake network, a fake self-update, a fake process start and a
// fake key press, against a real temporary game folder. main.cpp only builds
// the Windows AppIo (src/app_win.cpp) and calls run_app.
#pragma once

#include "net.h"
#include "selfupdate.h"
#include "signature.h"

#include <cstdio>
#include <string>
#include <vector>

namespace ffb {

// Fetched once per start. A 404 (not published yet) is "server unreachable".
extern const char* const kGamesJsonUrl;

// The package folder's name inside the game folder, and the games.json kept in
// it for offline starts (docs/SPEC.md, accepted default 3). The manifest of the
// last good update is kept beside it, so an offline start knows every required
// file of the package.
extern const char* const kPackageDirName;
extern const char* const kCachedGamesJson;
extern const char* const kCachedManifest;

// When it is the only argument, run_app prints the version line and exits 0
// before any network or folder work (the exe's own ctest checks use it).
extern const char* const kVersionSwitch;

// Everything the flow touches outside its own memory, except the game folder
// itself, which the finder and the package update read and write directly.
class AppIo {
public:
    virtual ~AppIo() = default;

    virtual Net& net() = 0;
    virtual SelfUpdateIo& self_update_io() = 0;

    // Starts `exe` with the whole command line `cmdline` (argv[0] included) and
    // `workdir` as its working directory, without waiting for it. All UTF-8.
    // -> false with `why` set when it would not start.
    virtual bool start_process(const std::string& exe, const std::string& cmdline,
                               const std::string& workdir, std::string* why) = 0;

    // Blocks until the player presses a key. Called after every error screen,
    // once "Press any key to exit." is on the screen.
    virtual void wait_key() = 0;

    // One console line each: `out` for the flow, `err` for warnings and errors.
    virtual void out(const std::string& line) = 0;
    virtual void err(const std::string& line) = 0;

    // Where the package update prints its progress lines; nullptr is silent.
    virtual std::FILE* package_log() { return stdout; }

    // The public keys games.json and the manifest must be signed with: the
    // compiled-in ones (src/trusted_keys.h). A test hands in its own.
    virtual const std::vector<PublicKey>& trusted_keys() { return ffb::trusted_keys(); }
};

struct AppInput {
    std::string game_folder;  // UTF-8: the folder FFB Co-op.exe sits in
    // Everything after argv[0] on this process's own command line, verbatim
    // (command_line_tail). Passed on to the package launcher unchanged.
    std::string arg_tail;
    Version running;          // running_version() outside tests
};

// The whole start. -> the process exit code: 0 when the package launcher was
// started (or a self-update restarted the exe), 1 after an error screen.
int run_app(const AppInput& in, AppIo& io);

#ifdef _WIN32
// run_app with the Windows AppIo, this exe's folder and its own command line
// (src/app_win.cpp). What main() returns.
int run_windows();
#endif

// --- the command line handed to the package launcher --------------------------

// A raw Windows command line without its first argument (the program name, by
// the MSVC runtime's rule: quotes toggle, a space or tab outside them ends it)
// and the blanks after it. The rest is returned verbatim, so
// every argument reaches the package launcher exactly as it was typed.
std::string command_line_tail(const std::string& raw);

// One argument, quoted so the MSVC runtime's argv parsing gives it back
// unchanged: left bare when it has no space, tab or quote, else in quotes with
// every quote and every backslash run before a quote (or the end) escaped.
std::string quote_arg(const std::string& arg);

// "<launcher>" "<game exe>" <tail>: the package launcher gets the game exe's
// full path as its first argument, then every argument FFB Co-op.exe was given.
std::string launcher_command_line(const std::string& launcher_path,
                                  const std::string& game_exe_path,
                                  const std::string& arg_tail);

}  // namespace ffb
