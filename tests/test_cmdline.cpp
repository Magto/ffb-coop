// test_cmdline.cpp -- the command line FFB Co-op.exe hands the package launcher
// (#6, docs/SPEC.md "The flow", step 5): the game exe's full path first, then
// every argument FFB Co-op.exe was given, unchanged and in order.
//
// Two kinds of check. Exact strings, derived by hand from the MSVC argv rules
// (Microsoft's "Parsing C++ command-line arguments"): 2n backslashes before a
// quote are n backslashes, 2n+1 are n and a literal quote, backslashes anywhere
// else are literal. And a round trip through a parser written here from those
// same rules, so a path the launcher gets back is the path that went in.
#include "ffb_test.h"
#include "app.h"

#include <string>
#include <vector>

namespace {

// The MSVC runtime's argv parsing, for the arguments after the program name.
std::vector<std::string> parse_args(const std::string& s) {
    std::vector<std::string> out;
    std::size_t i = 0;
    for (;;) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
        if (i >= s.size()) return out;
        std::string arg;
        bool quoted = false;
        while (i < s.size() && (quoted || (s[i] != ' ' && s[i] != '\t'))) {
            if (s[i] == '\\') {
                std::size_t n = 0;
                while (i < s.size() && s[i] == '\\') { ++n; ++i; }
                if (i < s.size() && s[i] == '"') {
                    arg.append(n / 2, '\\');
                    if (n % 2) { arg += '"'; ++i; }
                } else {
                    arg.append(n, '\\');
                }
                continue;
            }
            if (s[i] == '"') {
                if (quoted && i + 1 < s.size() && s[i + 1] == '"') { arg += '"'; i += 2; continue; }
                quoted = !quoted;
                ++i;
                continue;
            }
            arg += s[i++];
        }
        out.push_back(arg);
    }
}

// The whole command line, program name included, by the program-name rule
// (quotes toggle, no escapes) and then parse_args.
std::vector<std::string> parse_line(const std::string& s) {
    std::size_t i = 0;
    bool quoted = false;
    std::string prog;
    while (i < s.size() && (quoted || (s[i] != ' ' && s[i] != '\t'))) {
        if (s[i] == '"') quoted = !quoted;
        else prog += s[i];
        ++i;
    }
    std::vector<std::string> out{prog};
    for (auto& a : parse_args(s.substr(i))) out.push_back(a);
    return out;
}

const std::string kSteamGame =
    "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Mewgenics\\Mewgenics.exe";
const std::string kLoader =
    "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Mewgenics\\FFB Co-op\\mewcoop_loader.exe";

void test_quote_arg() {
    std::printf("quote_arg\n");
    CHECK(ffb::quote_arg(kSteamGame) == "\"" + kSteamGame + "\"");
    CHECK(ffb::quote_arg("C:\\Games\\Mewgenics.exe") == "C:\\Games\\Mewgenics.exe");
    CHECK(ffb::quote_arg("") == "\"\"");
    CHECK(ffb::quote_arg("a\"b") == "\"a\\\"b\"");              // a"b      -> "a\"b"
    CHECK(ffb::quote_arg("C:\\my dir\\") == "\"C:\\my dir\\\\\"");  // trailing \ doubled before the closing quote
    CHECK(ffb::quote_arg("x\\\\\"y") == "\"x\\\\\\\\\\\"y\"");  // x\\"y    -> "x\\\\\"y"
    CHECK(ffb::quote_arg("tab\there") == "\"tab\there\"");
}

void test_tail() {
    std::printf("command_line_tail\n");
    CHECK(ffb::command_line_tail("\"C:\\Games\\My Game\\FFB Co-op.exe\" -windowed \"a b\"") ==
          "-windowed \"a b\"");
    CHECK(ffb::command_line_tail("\"C:\\Games\\My Game\\FFB Co-op.exe\"") == "");
    CHECK(ffb::command_line_tail("\"C:\\Games\\My Game\\FFB Co-op.exe\"   ") == "");
    CHECK(ffb::command_line_tail("ffb.exe") == "");
    CHECK(ffb::command_line_tail("ffb.exe   -x  y") == "-x  y");      // inner spacing kept verbatim
    CHECK(ffb::command_line_tail("C:\\x\\ffb.exe\t-a") == "-a");
    CHECK(ffb::command_line_tail("\"C:\\a b\\ffb.exe\"x -y") == "-y");  // the name runs on after its quote
    CHECK(ffb::command_line_tail("") == "");
}

void test_launcher_line() {
    std::printf("launcher_command_line: the Steam path with spaces and parentheses\n");
    const std::string line = ffb::launcher_command_line(kLoader, kSteamGame, "");
    CHECK(line == "\"" + kLoader + "\" \"" + kSteamGame + "\"");
    const auto argv = parse_line(line);
    CHECK(argv.size() == 2);
    CHECK(argv.size() == 2 && argv[0] == kLoader && argv[1] == kSteamGame);
}

void test_passthrough() {
    std::printf("every argument FFB Co-op.exe got reaches the launcher unchanged, in order\n");
    // What a Steam shortcut or a player might type after FFB Co-op.exe.
    const std::string own = "\"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Mewgenics\\FFB Co-op.exe\""
                            " -windowed \"save dir\\with space\\\\\" a\\\"b \"\" last";
    const auto own_argv = parse_line(own);
    const std::string line = ffb::launcher_command_line(kLoader, kSteamGame, ffb::command_line_tail(own));
    const auto argv = parse_line(line);
    CHECK(own_argv.size() == 6);
    CHECK(argv.size() == own_argv.size() + 1);
    if (argv.size() == own_argv.size() + 1 && own_argv.size() == 6) {
        CHECK(argv[0] == kLoader);
        CHECK(argv[1] == kSteamGame);
        for (std::size_t i = 1; i < own_argv.size(); ++i) CHECK(argv[i + 1] == own_argv[i]);
        CHECK(argv[3] == "save dir\\with space\\");
        CHECK(argv[4] == "a\"b");
        CHECK(argv[5] == "");
        CHECK(argv[6] == "last");
    }
}

void test_quote_round_trip() {
    std::printf("quote_arg round-trips through the argv rules\n");
    const std::vector<std::string> samples = {
        kSteamGame, "", "plain", "two words", "a\"b", "\\\\server\\share\\x y\\", "x\\\\\"y", "end\\",
        "C:\\dir\\", "\"", "\\\"", "D:\\Spiele\\Mewgenics (Beta)\\Mewgenics.exe"};
    for (const auto& s : samples) {
        const auto back = parse_args(ffb::quote_arg(s));
        CHECK(back.size() == 1 && back[0] == s);
    }
}

}  // namespace

int main() {
    test_quote_arg();
    test_tail();
    test_launcher_line();
    test_passthrough();
    test_quote_round_trip();
    return ffb_test_result();
}
