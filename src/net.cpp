// net.cpp -- see net.h. Ported from Magto/mewgenics-coop loader/mewcoop_update.cpp
// (MIT, Copyright (c) 2026 SanTertrust; Copyright (c) 2026 Martin Strålenhielm for
// the Mewgenics Coop fork); the sha256 here is new, written from FIPS 180-4.
#include "net.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

namespace ffb {

// --- sha256 -----------------------------------------------------------------

namespace {

const std::uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

inline std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

Sha256::Sha256() {
    const std::uint32_t init[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::memcpy(h_, init, sizeof(h_));
}

void Sha256::block(const unsigned char* p) {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i)
        w[i] = (std::uint32_t)p[i * 4] << 24 | (std::uint32_t)p[i * 4 + 1] << 16 |
               (std::uint32_t)p[i * 4 + 2] << 8 | (std::uint32_t)p[i * 4 + 3];
    for (int i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
    for (int i = 0; i < 64; ++i) {
        const std::uint32_t t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + kK[i] + w[i];
        const std::uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d; h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += h;
}

void Sha256::feed(const void* data, std::size_t n) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    total_ += n;
    while (n > 0) {
        const std::size_t take = (64 - buf_len_ < n) ? 64 - buf_len_ : n;
        std::memcpy(buf_ + buf_len_, p, take);
        buf_len_ += take; p += take; n -= take;
        if (buf_len_ == 64) { block(buf_); buf_len_ = 0; }
    }
}

std::string Sha256::finish_hex() {
    const std::uint64_t bits = total_ * 8;
    const unsigned char one = 0x80, zero = 0;
    feed(&one, 1);
    while (buf_len_ != 56) feed(&zero, 1);
    unsigned char len[8];
    for (int i = 0; i < 8; ++i) len[i] = (unsigned char)(bits >> (56 - 8 * i));
    feed(len, 8);
    static const char digits[] = "0123456789abcdef";
    std::string hex;
    for (std::uint32_t v : h_)
        for (int shift = 28; shift >= 0; shift -= 4) hex += digits[(v >> shift) & 0xf];
    return hex;
}

std::string sha256_hex(const void* data, std::size_t n) {
    Sha256 s;
    s.feed(data, n);
    return s.finish_hex();
}

bool sha256_file(const std::string& path, std::string* hex, std::uint64_t* size) {
    std::error_code ec;
    const std::filesystem::path p = std::filesystem::u8path(path);
    if (!std::filesystem::is_regular_file(p, ec)) return false;
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    Sha256 s;
    std::vector<char> buf(1 << 20);
    std::uint64_t total = 0;
    while (in.read(buf.data(), (std::streamsize)buf.size()) || in.gcount() > 0) {
        s.feed(buf.data(), (std::size_t)in.gcount());
        total += (std::uint64_t)in.gcount();
        if (!in) break;
    }
    if (in.bad()) return false;
    *hex  = s.finish_hex();
    *size = total;
    return true;
}

bool is_sha256_hex(const std::string& s) {
    if (s.size() != 64) return false;
    for (char c : s)
        if (hex_value(c) < 0) return false;
    return true;
}

bool same_sha256(const std::string& a, const std::string& b) {
    if (!is_sha256_hex(a) || !is_sha256_hex(b)) return false;
    for (std::size_t i = 0; i < 64; ++i)
        if (hex_value(a[i]) != hex_value(b[i])) return false;
    return true;
}

// --- HTTP -------------------------------------------------------------------

int get_body(Net& net, const std::string& url, std::string* body) {
    body->clear();
    return net.get(url, [body](const char* p, std::size_t n) { body->append(p, n); return true; });
}

#ifdef _WIN32

namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

}  // namespace

WinHttpNet::WinHttpNet(const wchar_t* user_agent, unsigned receive_timeout_ms)
    : timeout_ms_(receive_timeout_ms) {
    session_ = WinHttpOpen(user_agent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                           WINHTTP_NO_PROXY_BYPASS, 0);
}

WinHttpNet::~WinHttpNet() {
    if (session_) WinHttpCloseHandle(session_);
}

int WinHttpNet::get(const std::string& url_utf8, const Sink& sink) {
    if (!session_) return 0;
    const std::wstring url = widen(url_utf8);

    // Null buffers with length -1: WinHttpCrackUrl points into `url` instead of
    // copying, so no fixed-size host or path buffer can truncate.
    URL_COMPONENTS uc = {sizeof(uc)};
    uc.dwSchemeLength = uc.dwHostNameLength = uc.dwUrlPathLength = uc.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return 0;
    if (uc.nScheme != INTERNET_SCHEME_HTTPS && uc.nScheme != INTERNET_SCHEME_HTTP) return 0;
    const std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
    std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
    path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    if (host.empty()) return 0;
    if (path.empty()) path = L"/";

    HINTERNET conn = WinHttpConnect(session_, host.c_str(), uc.nPort, 0);
    if (!conn) return 0;
    HINTERNET req = WinHttpOpenRequest(conn, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (!req) { WinHttpCloseHandle(conn); return 0; }
    // 3 s to resolve and connect: the host either answers at once or is not there.
    WinHttpSetTimeouts(req, 3000, 3000, (int)timeout_ms_, (int)timeout_ms_);

    DWORD status = 0;
    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(req, nullptr)) {
        WinHttpCloseHandle(req); WinHttpCloseHandle(conn);
        return 0;
    }
    DWORD sz = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    if (status == 200) {
        std::vector<char> buf(64 * 1024);
        for (;;) {
            DWORD n = 0;
            if (!WinHttpReadData(req, buf.data(), (DWORD)buf.size(), &n)) { status = 0; break; }
            if (n == 0) break;
            if (!sink(buf.data(), n)) break;
        }
    }
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    return (int)status;
}

#endif  // _WIN32

}  // namespace ffb
