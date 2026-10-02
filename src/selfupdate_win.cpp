// selfupdate_win.cpp -- the Windows SelfUpdateIo: WinHTTP for the download,
// BCrypt for the sha256, MoveFileEx for the swap, CreateProcess for the
// restart. Ported from mewgenics-coop loader/mewcoop_update.cpp (Http,
// http_get, Sha256, replace_self_and_restart, update_sweep); the MIT notice and
// credit are in selfupdate.h.
//
// The HTTP GET and the sha256 here are a minimal copy for #5 alone. The package
// download (#4) needs the same two; when it lands with a shared helper, this file
// should use that one instead of its own.
#include "selfupdate.h"

#include "channel.h"

#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace ffb {
namespace {

// Set in the child's environment by restart(): "you are the update".
const wchar_t* kRestartedFlag = L"FFB_COOP_UPDATED";

// The exe is well under a megabyte; a minute is plenty over any real line.
const DWORD kConnectTimeoutMs  = 3000;
const DWORD kTransferTimeoutMs = 60000;

std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

// sha256 over BCrypt, fed as the bytes arrive: the download is verified by what
// was actually written, never by a second read that could see another file.
struct Sha256 {
    BCRYPT_ALG_HANDLE  alg  = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;

    bool begin() {
        if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return false;
        return BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    }
    void feed(const void* p, size_t n) {
        if (hash) BCryptHashData(hash, (PUCHAR)p, (ULONG)n, 0);
    }
    bool finish(std::string* hex) {
        UCHAR digest[32] = {};
        if (!hash || BCryptFinishHash(hash, digest, 32, 0) < 0) return false;
        char buf[65] = {};
        for (int i = 0; i < 32; ++i) sprintf_s(buf + i * 2, 3, "%02x", digest[i]);
        *hex = buf;
        return true;
    }
    ~Sha256() {
        if (hash) BCryptDestroyHash(hash);
        if (alg)  BCryptCloseAlgorithmProvider(alg, 0);
    }
};

// Closes a WinHTTP handle on every return path.
struct Handle {
    HINTERNET h = nullptr;
    explicit Handle(HINTERNET x) : h(x) {}
    ~Handle() { if (h) WinHttpCloseHandle(h); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

class WindowsSelfUpdateIo : public SelfUpdateIo {
public:
    bool is_restarted_child() override {
        wchar_t flag[8] = {};
        if (GetEnvironmentVariableW(kRestartedFlag, flag, 8) > 0 && flag[0] == L'1') {
            // Read once, then cleared, so the package launcher this process
            // starts later does not inherit it.
            SetEnvironmentVariableW(kRestartedFlag, nullptr);
            return true;
        }
        return false;
    }

    std::wstring self_path() override {
        std::vector<wchar_t> buf(MAX_PATH);
        for (;;) {
            DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
            if (n == 0) return std::wstring();
            if (n < buf.size()) return std::wstring(buf.data(), n);
            buf.resize(buf.size() * 2);
        }
    }

    bool download(const std::string& url, const std::wstring& dest,
                  std::string* sha256_hex, std::uint64_t* got) override {
        *got = 0;
        const std::wstring wurl = widen(url);

        URL_COMPONENTS uc = { sizeof(uc) };
        uc.dwHostNameLength  = (DWORD)-1;
        uc.dwUrlPathLength   = (DWORD)-1;
        uc.dwExtraInfoLength = (DWORD)-1;
        if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc) || uc.nScheme != INTERNET_SCHEME_HTTPS ||
            uc.dwHostNameLength == 0) {
            warn("self-update: not an https URL: " + url);
            return false;
        }
        const std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
        std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
        if (uc.lpszExtraInfo) path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
        if (path.empty()) path = L"/";

        Handle session(WinHttpOpen(L"FFBCoop/1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        if (!session.h) return false;
        Handle conn(WinHttpConnect(session.h, host.c_str(), uc.nPort, 0));
        if (!conn.h) return false;
        Handle req(WinHttpOpenRequest(conn.h, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
        if (!req.h) return false;
        WinHttpSetTimeouts(req.h, kConnectTimeoutMs, kConnectTimeoutMs, kTransferTimeoutMs,
                           kTransferTimeoutMs);

        // The dev exe's own update comes from behind the dev login (#26); the
        // same rule as WinHttpNet: under the dev prefix only, never redirected.
        const std::wstring login = widen(process_login_header(url));
        if (!login.empty()) {
            DWORD never = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
            WinHttpSetOption(req.h, WINHTTP_OPTION_REDIRECT_POLICY, &never, sizeof(never));
        }

        if (!WinHttpSendRequest(req.h, login.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : login.c_str(),
                                login.empty() ? 0 : (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(req.h, nullptr))
            return false;
        DWORD status = 0, sz = sizeof(status);
        WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
        if (status != 200) {
            warn("self-update: the server answered HTTP " + std::to_string(status));
            return false;
        }

        Sha256 s;
        if (!s.begin()) return false;
        std::ofstream out(dest.c_str(), std::ios::binary | std::ios::trunc);
        if (!out) return false;

        std::vector<char> buf(64 * 1024);
        for (;;) {
            DWORD n = 0;
            if (!WinHttpReadData(req.h, buf.data(), (DWORD)buf.size(), &n)) return false;
            if (n == 0) break;
            s.feed(buf.data(), n);
            out.write(buf.data(), n);
            if (!out) return false;
            *got += n;
        }
        out.close();
        if (!out) return false;
        return s.finish(sha256_hex);
    }

    bool move_replace(const std::wstring& from, const std::wstring& to) override {
        if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING)) return true;
        const DWORD err = GetLastError();
        warn("self-update: could not move " + narrow(from) + " to " + narrow(to) + " (error " +
             std::to_string(err) + ")");
        return false;
    }

    void remove(const std::wstring& path) override {
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return;
        // The parent that left an .old.exe behind may still be exiting and hold
        // its image open for a moment. Five tries over most of a second; what is
        // left is swept on the next start, so this never delays anyone.
        for (int i = 0; i < 5 && !DeleteFileW(path.c_str()); ++i) Sleep(150);
    }

    bool restart(const std::wstring& exe) override {
        // The child must not check again: one failed swap that left the old
        // version in place would otherwise be an endless relaunch.
        SetEnvironmentVariableW(kRestartedFlag, L"1");

        // GetCommandLineW rather than a rebuild from argv: the arguments pass
        // through untouched, and re-quoting them is a bug waiting for the first
        // path with a space in it -- "FFB Co-op.exe" is one. lpApplicationName
        // is the full path, so argv[0] in the line is only what the child reads.
        std::wstring cmd = GetCommandLineW();
        std::vector<wchar_t> mutable_cmd(cmd.begin(), cmd.end());
        mutable_cmd.push_back(0);

        STARTUPINFOW        si = { sizeof(si) };
        PROCESS_INFORMATION pi = {};
        // Handles inherited and no new console: the restarted exe keeps writing
        // into the window the player is already looking at. Working directory
        // inherited too, so the restart starts exactly as this process did.
        const BOOL ok = CreateProcessW(exe.c_str(), mutable_cmd.data(), nullptr, nullptr, TRUE, 0,
                                       nullptr, nullptr, &si, &pi);
        SetEnvironmentVariableW(kRestartedFlag, nullptr);
        if (!ok) {
            warn("self-update: the new exe would not start (error " +
                 std::to_string(GetLastError()) + ")");
            return false;
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }

    void note(const std::string& line) override { std::printf("%s\n", line.c_str()); }
    void warn(const std::string& line) override { std::fprintf(stderr, "%s\n", line.c_str()); }
};

} // namespace

SelfUpdateIo& windows_self_update_io() {
    static WindowsSelfUpdateIo io;
    return io;
}

} // namespace ffb
