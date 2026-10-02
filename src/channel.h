// channel.h -- which coopmods.com channel this exe reads, and where it installs
// (#26, docs/SPEC.md "Dev channel").
//
// One source, two exes. `FFB Co-op.exe` is the public channel. `FFB Co-op -
// dev.exe` is the same code built with FFB_DEV_CHANNEL defined (the
// ffb_coop_dev target in CMakeLists.txt): it reads games.json from
// coopmods.com/dev/, behind a per-person login, and installs into
// `FFB Co-op dev\` beside the public `FFB Co-op\`. Everything else -- the
// signature check, the self-update rules, the package update -- is the same
// code with these values in place of the public ones.
//
// Both channels are always compiled in, so the unit tests run the dev values
// against the real flow without a second build; the build switch only picks
// which one this_channel() answers.
#pragma once

#include <string>

namespace ffb {

struct Channel {
    const char* name;            // "public" or "dev"
    const char* exe_name;        // the file players see: "FFB Co-op.exe"
    const char* games_json_url;  // fetched once per start
    const char* package_dir;     // the package folder's name inside the game folder
    // Every URL under this prefix is fetched with the dev login; nothing else
    // ever is. nullptr: this channel sends no login at all.
    const char* login_prefix;
    // Set in the environment the package launcher inherits ("NAME=VALUE"), or
    // nullptr. The dev channel sets MEWCOOP_NOUPDATE=1: the dev exe is the
    // package's updater, and the Mewgenics loader's own update would put the
    // public build back over the dev one.
    const char* launcher_env;
};

// The public channel's two names, also exported by app.h as kGamesJsonUrl and
// kPackageDirName. Constants, so those are set before any other file's
// statics read them.
inline constexpr char kPublicGamesJsonUrl[] = "https://coopmods.com/games.json";
inline constexpr char kPublicPackageDir[]   = "FFB Co-op";

const Channel& public_channel();
const Channel& dev_channel();

// The channel this exe was built for: dev_channel() with FFB_DEV_CHANNEL
// defined, public_channel() otherwise.
const Channel& this_channel();

// --- the dev login ----------------------------------------------------------
//
// HTTP Basic, per person ("martin:<password>", "budda:<password>"), checked by
// Caddy's basic_auth on coopmods.com/dev/* (site/README.md). Nothing secret is
// compiled into either exe: the dev exe asks for the login once and keeps it in
// its own package folder, encrypted to the Windows account (src/app_win.cpp).

// RFC 4648 base64 with padding.
std::string base64(const std::string& bytes);

// The header line to send with a GET of `url`: "Authorization: Basic <...>" when
// the channel has a login prefix, `url` starts with it, and `login` is a
// "user:password" with a non-empty user; "" otherwise. Only an https prefix is
// ever set, so a login never travels in the clear.
std::string login_header(const Channel& ch, const std::string& login, const std::string& url);

// The login the Windows downloaders send (WinHttpNet and the self-update):
// set once at start by the dev exe, never by the public one. Empty by default.
void set_process_login(const Channel& ch, const std::string& login);
// login_header() for `url` with the channel and login set above.
std::string process_login_header(const std::string& url);

}  // namespace ffb
