// app_win.cpp -- the Windows AppIo: WinHTTP for games.json and the package,
// the self-update's own Windows side, CreateProcess for the package launcher,
// and the console for lines and the key press. See app.h.
#include "app.h"
#include "ffb_version.h"

#include <windows.h>
#include <conio.h>

#include <cstdio>
#include <string>
#include <vector>

namespace ffb {
namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

bool is_console(DWORD which) {
    DWORD mode = 0;
    return GetConsoleMode(GetStdHandle(which), &mode) != 0;
}

// FFB_COOP_OFFLINE=1 -- for the exe's own ctest checks only: every fetch fails
// as if there were no network, so a test can never reach coopmods.com, and with
// no games.json there is no self-update of the build either.
bool offline_for_tests() {
    wchar_t flag[8] = {};
    return GetEnvironmentVariableW(L"FFB_COOP_OFFLINE", flag, 8) > 0 && flag[0] == L'1';
}

class NoNet : public Net {
public:
    int get(const std::string&, const Sink&) override { return 0; }
};

class WindowsAppIo : public AppIo {
public:
    WindowsAppIo() : net_(L"FFBCoop/1"), offline_(offline_for_tests()) {}

    Net& net() override { return offline_ ? static_cast<Net&>(no_net_) : net_; }
    SelfUpdateIo& self_update_io() override { return windows_self_update_io(); }

    bool start_process(const std::string& exe, const std::string& cmdline,
                       const std::string& workdir, std::string* why) override {
        const std::wstring wexe = widen(exe), wdir = widen(workdir);
        std::wstring wcmd = widen(cmdline);
        std::vector<wchar_t> mutable_cmd(wcmd.begin(), wcmd.end());
        mutable_cmd.push_back(0);

        // No new console: the package launcher writes into the window the
        // player is already looking at, and keeps it open after this exits.
        STARTUPINFOW        si = {sizeof(si)};
        PROCESS_INFORMATION pi = {};
        if (!CreateProcessW(wexe.c_str(), mutable_cmd.data(), nullptr, nullptr, FALSE, 0, nullptr,
                            wdir.c_str(), &si, &pi)) {
            *why = "error " + std::to_string(GetLastError());
            return false;
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }

    void wait_key() override {
        std::fflush(stdout);
        std::fflush(stderr);
        // Only a player at a console can press a key. With stdin or stdout
        // redirected (ctest, a script) nobody is looking, and waiting would hang.
        if (!is_console(STD_INPUT_HANDLE) || !is_console(STD_OUTPUT_HANDLE)) return;
        FlushConsoleInputBuffer(GetStdHandle(STD_INPUT_HANDLE));
        _getch();
    }

    // Flushed per line, so stdout and stderr keep their order in a pipe too.
    void out(const std::string& line) override {
        std::printf("%s\n", line.c_str());
        std::fflush(stdout);
    }
    void err(const std::string& line) override {
        std::fflush(stdout);
        std::fprintf(stderr, "%s\n", line.c_str());
        std::fflush(stderr);
    }

private:
    WinHttpNet net_;
    NoNet      no_net_;
    bool       offline_;
};

std::wstring self_folder() {
    std::vector<wchar_t> buf(MAX_PATH);
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
        if (n == 0) return std::wstring();
        if (n < buf.size()) {
            std::wstring path(buf.data(), n);
            const size_t slash = path.find_last_of(L"\\/");
            return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
        }
        buf.resize(buf.size() * 2);
    }
}

}  // namespace

// Called by main.cpp: the game folder is the folder the exe sits in, never the
// working directory, which a shortcut can set to anywhere.
int run_windows() {
    SetConsoleOutputCP(CP_UTF8);   // game folders can hold any character
    SetConsoleTitleW(widen(ffb_window_title()).c_str());   // "FFB Co-op v1" (#24)
    AppInput in;
    in.game_folder = narrow(self_folder());
    in.arg_tail    = command_line_tail(narrow(GetCommandLineW()));
    in.running     = running_version();
    WindowsAppIo io;
    return run_app(in, io);
}

}  // namespace ffb
