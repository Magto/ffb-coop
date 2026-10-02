// app.cpp -- see app.h. The order is docs/SPEC.md "The flow":
//
//   1. games.json from coopmods.com, and games.json.sig beside it (#21).
//      Unreachable (a 404 included), unsigned, badly signed or invalid: the
//      offline start, from the games.json kept in FFB Co-op\. A file that is not
//      signed by a trusted key is never parsed, so it can never self-update.
//   2. Self-update. A restart ends this process; a failure has already warned
//      and the flow carries on.
//   3. Find the game: none and more than one are error screens.
//   4. Bring the package up to date (its manifest is signature-checked the
//      same way), then keep games.json in FFB Co-op\ -- unless the manifest
//      came unsigned. Any failure there is the offline start for the game just
//      found.
//   5. Start FFB Co-op\<launcher> "<game exe>" <every argument>, working
//      directory FFB Co-op\, and exit without waiting.
//
// Every error screen ends with "Press any key to exit." and a key press, and
// exits 1.
#include "app.h"

#include "ffb_version.h"
#include "finder.h"
#include "games.h"
#include "package.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace ffb {

const char* const kGamesJsonUrl    = "https://coopmods.com/games.json";
const char* const kPackageDirName  = "FFB Co-op";
const char* const kCachedGamesJson = "games.json";
const char* const kCachedManifest  = "manifest.json";
const char* const kVersionSwitch   = "--version";

namespace {

std::string join_path(const std::string& dir, const std::string& name) {
    return (fs::u8path(dir) / fs::u8path(name)).u8string();
}

std::string package_dir(const AppInput& in) { return join_path(in.game_folder, kPackageDirName); }

bool read_text(const std::string& path, std::string* out) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    *out = ss.str();
    return true;
}

// Written to games.json.new and moved over, so a start that dies halfway never
// leaves a torn copy for the next offline start.
bool write_text(const std::string& path, const std::string& text) {
    const fs::path p = fs::u8path(path), tmp = fs::u8path(path + ".new");
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << text;
        f.close();
        if (!f) return false;
    }
    std::error_code ec;
    fs::rename(tmp, p, ec);
    if (ec) fs::remove(tmp, ec);
    return !ec;
}

int error_screen(AppIo& io, const std::vector<std::string>& lines) {
    for (const auto& l : lines) io.err(l);
    io.err("Press any key to exit.");
    io.wait_key();
    return 1;
}

std::string exe_list(const std::vector<GameEntry>& games) {
    std::string s;
    for (const auto& g : games) s += (s.empty() ? "" : ", ") + g.exe;
    return s;
}

int none_screen(const AppInput& in, AppIo& io, const std::vector<GameEntry>& games) {
    return error_screen(io, {"Could not find a FFB modded game in " + in.game_folder,
                             "Looking for: " + exe_list(games),
                             "Put FFB Co-op.exe next to the game's exe and start it again."});
}

int many_screen(const AppInput& in, AppIo& io, const std::vector<GameEntry>& matches) {
    return error_screen(io, {"Found more than one FFB modded game in " + in.game_folder + ": " +
                                 exe_list(matches),
                             "FFB Co-op.exe can only serve one game per folder."});
}

int not_installed_screen(const AppInput& in, AppIo& io, const std::string& prefix) {
    return error_screen(io, {prefix + ", and FFB Co-op is not installed in " + in.game_folder +
                             " yet. Connect to the internet and start it again."});
}

int start_launcher(const AppInput& in, AppIo& io, const GameEntry& game) {
    const std::string dir      = package_dir(in);
    const std::string launcher = join_path(dir, game.launcher);
    const std::string game_exe = join_path(in.game_folder, game.exe);
    const std::string cmdline  = launcher_command_line(launcher, game_exe, in.arg_tail);
    io.out("Starting " + game.name + " with co-op: " + game.launcher);
    std::string why;
    if (!io.start_process(launcher, cmdline, dir, game.steam_appid, &why))
        return error_screen(io, {"Could not start " + launcher + " (" + why + ")"});
    return 0;
}

// Installed means every required file of the package is in the package folder
// (docs/SPEC.md, Offline), by `fresh` -- the manifest just fetched -- or else by
// the manifest kept from the last good update. No manifest, no install. The
// package launcher is checked too, whatever the manifest says.
bool package_installed(const AppInput& in, const GameEntry& game, const Manifest* fresh) {
    Manifest kept;
    if (!fresh) {
        std::string text, why;
        if (!read_text(join_path(package_dir(in), kCachedManifest), &text) ||
            !parse_manifest(text, &kept, &why))
            return false;
        fresh = &kept;
    }
    std::error_code ec;
    auto present = [&](const std::string& name) {
        return fs::is_regular_file(fs::u8path(join_path(package_dir(in), name)), ec);
    };
    if (!present(game.launcher)) return false;
    for (const auto& f : fresh->files)
        if (f.required && !present(f.name)) return false;
    return true;
}

// Offline, for a game already found: the installed package with one warning
// line, or the not-installed screen.
int offline_start_game(const AppInput& in, AppIo& io, const std::string& prefix,
                       const GameEntry& game, const Manifest* fresh = nullptr) {
    if (!package_installed(in, game, fresh)) return not_installed_screen(in, io, prefix);
    io.err(prefix + " -- starting the installed version.");
    return start_launcher(in, io, game);
}

// Offline before the game is known: find it from the games.json kept in
// FFB Co-op\. No copy, or one that does not read, is "not installed".
int offline_start(const AppInput& in, AppIo& io, const std::string& prefix) {
    std::string text;
    if (!read_text(join_path(package_dir(in), kCachedGamesJson), &text))
        return not_installed_screen(in, io, prefix);
    const GamesJsonResult cached = parse_games_json(text);
    if (!cached.ok) return not_installed_screen(in, io, prefix);

    const FindResult found = find_game(fs::u8path(in.game_folder), cached.value.games);
    if (found.outcome == FindOutcome::None) return none_screen(in, io, cached.value.games);
    if (found.outcome == FindOutcome::Many) return many_screen(in, io, found.matches);
    return offline_start_game(in, io, prefix, found.matches.front());
}

const char* const kUnreachable = "Could not reach coopmods.com";

std::string unreadable(const std::string& reason) {
    return "coopmods.com sent a file this version cannot read (" + reason + ")";
}

std::string unsigned_file(const std::string& reason) {
    return "coopmods.com sent a file without a valid signature (" + reason + ")";
}

}  // namespace

int run_app(const AppInput& in, AppIo& io) {
    io.out(ffb_version_line());
    // `FFB Co-op.exe --version`, as the only argument: the version line and
    // nothing else -- no network, no folder. Anything more is passed on.
    if (in.arg_tail.substr(0, in.arg_tail.find_last_not_of(" \t") + 1) == kVersionSwitch) return 0;
    self_update_sweep(io.self_update_io());

    // --- 1. games.json ---
    std::string body;
    if (get_body(io.net(), kGamesJsonUrl, &body) != 200) return offline_start(in, io, kUnreachable);
    // Its signature, over the exact bytes, before any of them is parsed.
    const std::string sig_url = signature_url(kGamesJsonUrl);
    std::string sig;
    const int sig_status = get_body(io.net(), sig_url, &sig);
    if (sig_status == 0) return offline_start(in, io, kUnreachable);
    const std::string bad_sig =
        sig_status != 200 ? "games.json is not signed: " + sig_url + " said HTTP " + std::to_string(sig_status)
                          : check_signature("games.json", body, sig, io.trusted_keys());
    if (!bad_sig.empty()) return offline_start(in, io, unsigned_file(bad_sig));
    const GamesJsonResult parsed = parse_games_json(body);
    if (!parsed.ok) return offline_start(in, io, unreadable(parsed.error));
    const GamesJson& gj = parsed.value;

    // --- 2. self-update ---
    LauncherBlock block;
    block.version = gj.launcher.version;
    block.url     = gj.launcher.url;
    block.sha256  = gj.launcher.sha256;
    block.size    = gj.launcher.size;
    if (self_update(block, in.running, io.self_update_io()) == SelfUpdateOutcome::Restart) return 0;

    // --- 3. find the game ---
    const FindResult found = find_game(fs::u8path(in.game_folder), gj.games);
    if (found.outcome == FindOutcome::None) return none_screen(in, io, gj.games);
    if (found.outcome == FindOutcome::Many) return many_screen(in, io, found.matches);
    const GameEntry& game = found.matches.front();

    // --- 4. update the package, keep games.json ---
    const std::string dir = package_dir(in);
    const PackageResult pkg = update_package(dir, game.manifest, game.launcher, io.net(),
                                             io.package_log(), io.trusted_keys());
    // Kept for offline starts -- except after an unsigned manifest, which
    // changes nothing in the game folder (#21).
    if (pkg.outcome != PackageOutcome::Unsigned) {
        std::error_code ec;
        fs::create_directories(fs::u8path(dir), ec);
        if (!write_text(join_path(dir, kCachedGamesJson), body))
            io.err("Could not save " + join_path(dir, kCachedGamesJson) +
                   " -- an offline start will not find the game.");
    }
    switch (pkg.outcome) {
    case PackageOutcome::Current:
        // Kept for offline starts: the list of required files (accepted default 3
        // keeps games.json the same way).
        if (!write_text(join_path(dir, kCachedManifest), pkg.manifest_text))
            io.err("Could not save " + join_path(dir, kCachedManifest) +
                   " -- an offline start will not find the package.");
        break;
    // Unreachable or Invalid: the manifest kept from the last good update decides.
    case PackageOutcome::Unreachable: return offline_start_game(in, io, kUnreachable, game);
    case PackageOutcome::Invalid:     return offline_start_game(in, io, unreadable(pkg.reason), game);
    case PackageOutcome::Unsigned:    return offline_start_game(in, io, unsigned_file(pkg.reason), game);
    // Failed: the manifest just fetched is valid and names every required file.
    case PackageOutcome::Failed:
        return offline_start_game(in, io, "Could not update FFB Co-op (" + pkg.reason + ")", game,
                                  &pkg.manifest);
    }

    // --- 5. start ---
    return start_launcher(in, io, game);
}

// --- command line ---------------------------------------------------------------

std::string command_line_tail(const std::string& raw) {
    // The MSVC runtime's rule for the program name: a quote toggles quoting and
    // is never escaped; a space or tab outside quotes ends the name.
    std::size_t i = 0;
    bool quoted = false;
    while (i < raw.size() && (quoted || (raw[i] != ' ' && raw[i] != '\t'))) {
        if (raw[i] == '"') quoted = !quoted;
        ++i;
    }
    while (i < raw.size() && (raw[i] == ' ' || raw[i] == '\t')) ++i;
    return raw.substr(i);
}

std::string quote_arg(const std::string& arg) {
    if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos) return arg;
    std::string q = "\"";
    std::size_t backslashes = 0;
    for (char c : arg) {
        if (c == '\\') { ++backslashes; continue; }
        // Backslashes are literal unless a quote follows: then each one doubles,
        // and the quote itself is escaped.
        q.append(c == '"' ? backslashes * 2 + 1 : backslashes, '\\');
        backslashes = 0;
        q += c;
    }
    q.append(backslashes * 2, '\\');   // before the closing quote
    return q + "\"";
}

std::string launcher_command_line(const std::string& launcher_path,
                                  const std::string& game_exe_path,
                                  const std::string& arg_tail) {
    std::string cmd = quote_arg(launcher_path) + " " + quote_arg(game_exe_path);
    if (!arg_tail.empty()) cmd += " " + arg_tail;
    return cmd;
}

}  // namespace ffb
