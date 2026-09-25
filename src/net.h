// net.h -- sha256 and HTTP GET, the two things every download in FFB Co-op.exe
// needs: the package update (#4) and the self-update (#5).
//
// The network sits behind the Net interface so the update code can be tested
// with no network: a test hands it a fake that serves bytes from memory. The
// real one, WinHttpNet, exists on Windows only.
//
// Ported from Magto/mewgenics-coop loader/mewcoop_update.cpp (MIT, Copyright (c)
// 2026 SanTertrust; Copyright (c) 2026 Martin Strålenhielm for the Mewgenics Coop
// fork). What changed: the sha256 is a portable implementation instead of BCrypt,
// so the unit tests also run off Windows, and the GET streams into a callback
// instead of a file, so the caller decides where the bytes go and checks them.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace ffb {

// --- sha256 (FIPS 180-4) ----------------------------------------------------
//
// The hash is what stands between the manifest and arbitrary bytes landing in
// the package folder, so a download is hashed as its bytes arrive -- never on a
// second read of the file, which could see a different file.
class Sha256 {
public:
    Sha256();
    void feed(const void* data, std::size_t n);
    // -> 64 lowercase hex digits. Call once; the object is spent afterwards.
    std::string finish_hex();

private:
    void block(const unsigned char* p);
    std::uint32_t h_[8];
    unsigned char buf_[64];
    std::size_t   buf_len_ = 0;
    std::uint64_t total_   = 0;
};

std::string sha256_hex(const void* data, std::size_t n);
inline std::string sha256_hex(const std::string& s) { return sha256_hex(s.data(), s.size()); }

// sha256 of a file on disk. -> false when it cannot be read (missing, locked,
// a directory). `path` is UTF-8.
bool sha256_file(const std::string& path, std::string* hex, std::uint64_t* size);

// True when `s` is exactly 64 hex digits, either case.
bool is_sha256_hex(const std::string& s);
// Two sha256 hex strings are the same hash; the spec compares without regard to case.
bool same_sha256(const std::string& a, const std::string& b);

// --- HTTP -------------------------------------------------------------------

// Receives the body as it arrives. Return false to stop the transfer (the
// caller has seen too many bytes, or could not write them).
using Sink = std::function<bool(const char* data, std::size_t n)>;

class Net {
public:
    virtual ~Net() = default;
    // One GET of `url` (UTF-8). The body goes to `sink` only on HTTP 200.
    // -> the HTTP status, or 0 for a transport failure (no network, no host,
    // TLS, a read that timed out). A sink that stops the transfer does not
    // change the status; the caller knows why it stopped.
    virtual int get(const std::string& url, const Sink& sink) = 0;
};

// A GET into memory. -> the HTTP status, as Net::get.
int get_body(Net& net, const std::string& url, std::string* body);

#ifdef _WIN32
// WinHTTP, the system proxy, https and http. `receive_timeout_ms` bounds each
// read, not the whole transfer, so a 45 MB file on a slow line still finishes.
class WinHttpNet : public Net {
public:
    explicit WinHttpNet(const wchar_t* user_agent, unsigned receive_timeout_ms = 30000);
    ~WinHttpNet() override;
    int get(const std::string& url, const Sink& sink) override;

private:
    void*    session_ = nullptr;   // HINTERNET
    unsigned timeout_ms_;
};
#endif

}  // namespace ffb
