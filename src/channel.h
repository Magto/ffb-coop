// channel.h -- which ffbcoop.com channel this exe reads, and where it installs
// (#26, docs/SPEC.md "Dev channel"), and the server's two names (#35).
//
// One source, two exes. `FFB Co-op.exe` is the public channel. `FFB Co-op -
// dev.exe` is the same code built with FFB_DEV_CHANNEL defined (the
// ffb_coop_dev target in CMakeLists.txt): it reads games.json from
// ffbcoop.com/dev/, behind one shared password, and installs into
// `FFB Co-op dev\` beside the public `FFB Co-op\`. Everything else -- the
// signature check, the self-update rules, the package update -- is the same
// code with these values in place of the public ones.
//
// Both channels are always compiled in, so the unit tests run the dev values
// against the real flow without a second build; the build switch only picks
// which one this_channel() answers.
#pragma once

#include <functional>
#include <string>
#include <vector>

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
inline constexpr char kPublicGamesJsonUrl[] = "https://ffbcoop.com/games.json";
inline constexpr char kPublicPackageDir[]   = "FFB Co-op";

const Channel& public_channel();
const Channel& dev_channel();

// The channel this exe was built for: dev_channel() with FFB_DEV_CHANNEL
// defined, public_channel() otherwise.
const Channel& this_channel();

// --- the server's two names (#35, docs/SPEC.md "Server") --------------------
//
// One server answers to both names with the same files: ffbcoop.com and
// coopmods.com, www. and mewgenics. under each. Some players' ISPs block the
// name coopmods.com, so every fetch tries the ffbcoop.com form first and the
// coopmods.com form only when ffbcoop.com does not answer at all. One rule,
// first_answer() below, used by both downloaders: MirrorNet (src/net.h) and the
// self-update (src/selfupdate_win.cpp). An HTTP status -- a 404 or a 401
// included -- is the server's answer and is never retried.

// The host of an https URL, lowercased; "" when it is not https or carries a
// port or a user name.
std::string url_host(const std::string& url);

// The URLs to try for `url`, in order: its ffbcoop.com form, then its
// coopmods.com form, for a URL on either name (coopmods.com, www.coopmods.com,
// mewgenics.coopmods.com and their ffbcoop.com twins); {url} for anything else.
std::vector<std::string> mirror_urls(const std::string& url);

// Tries mirror_urls(url) in order until one answers. `attempt` fetches one URL
// and returns true when the server sent a response of any kind (any status, or
// the first byte); then nothing more is tried. `on_skip(host, next_host)` is
// called for each name that did not answer, before the next is tried.
// -> the URL that answered, or "" when none did.
std::string first_answer(const std::string& url,
                         const std::function<bool(const std::string& url)>& attempt,
                         const std::function<void(const std::string& host, const std::string& next)>& on_skip);

// --- the dev login ----------------------------------------------------------
//
// HTTP Basic with one fixed credential, "dev:<the shared password>", checked by
// Caddy's basic_auth on ffbcoop.com/dev/* and coopmods.com/dev/* (site/README.md). The password is
// built into the dev exe only, from outside the repo (cmake/dev_password.cmake);
// the public exe has none and sends nothing.

// The user name of the one dev credential.
inline constexpr char kDevLoginUser[] = "dev";

// RFC 4648 base64 with padding.
std::string base64(const std::string& bytes);

// The header line to send with a GET of `url`: "Authorization: Basic <...>" when
// the channel has a login prefix, `url` -- or its ffbcoop.com form, so the
// coopmods.com fallback carries it too -- starts with it, and `login` is a
// "user:password" with a non-empty user; "" otherwise. Only an https prefix is
// ever set, so a login never travels in the clear.
std::string login_header(const Channel& ch, const std::string& login, const std::string& url);

// The login the Windows downloaders send (WinHttpNet and the self-update):
// set once at start by the dev exe (src/app_win.cpp), never by the public one.
// Empty by default.
void set_process_login(const Channel& ch, const std::string& login);
// login_header() for `url` with the channel and login set above.
std::string process_login_header(const std::string& url);

}  // namespace ffb
