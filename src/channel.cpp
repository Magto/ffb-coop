// channel.cpp -- see channel.h.
#include "channel.h"

#include <cctype>

namespace ffb {

namespace {

const Channel kPublic = {
    "public",
    "FFB Co-op.exe",
    kPublicGamesJsonUrl,
    kPublicPackageDir,
    nullptr,
    nullptr,
};

const Channel kDev = {
    "dev",
    "FFB Co-op - dev.exe",
    "https://ffbcoop.com/dev/games.json",
    "FFB Co-op dev",
    "https://ffbcoop.com/dev/",
    "MEWCOOP_NOUPDATE=1",
};

const Channel* g_login_channel = nullptr;
std::string    g_login;

bool starts_with(const std::string& s, const char* prefix) {
    const std::string p(prefix);
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

const char kHttps[] = "https://";

// The names the one server answers to, as {ffbcoop.com, coopmods.com} pairs.
const char* const kTwins[][2] = {
    {"ffbcoop.com", "coopmods.com"},
    {"www.ffbcoop.com", "www.coopmods.com"},
    {"mewgenics.ffbcoop.com", "mewgenics.coopmods.com"},
};

}  // namespace

const Channel& public_channel() { return kPublic; }
const Channel& dev_channel() { return kDev; }

const Channel& this_channel() {
#ifdef FFB_DEV_CHANNEL
    return kDev;
#else
    return kPublic;
#endif
}

std::string url_host(const std::string& url) {
    const std::size_t start = sizeof(kHttps) - 1;
    if (url.size() < start) return std::string();
    for (std::size_t i = 0; i < start; ++i)
        if (std::tolower((unsigned char)url[i]) != kHttps[i]) return std::string();
    const std::size_t end = url.find_first_of("/?#", start);
    std::string host = url.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (host.find_first_of(":@") != std::string::npos) return std::string();
    for (char& c : host) c = (char)std::tolower((unsigned char)c);
    return host;
}

std::vector<std::string> mirror_urls(const std::string& url) {
    const std::string host = url_host(url);
    if (!host.empty())
        for (const auto& twin : kTwins)
            if (host == twin[0] || host == twin[1]) {
                const std::string rest = url.substr(sizeof(kHttps) - 1 + host.size());
                return {kHttps + std::string(twin[0]) + rest, kHttps + std::string(twin[1]) + rest};
            }
    return {url};
}

std::string base64(const std::string& bytes) {
    static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::size_t i = 0;
    for (; i + 3 <= bytes.size(); i += 3) {
        const unsigned v = (unsigned char)bytes[i] << 16 | (unsigned char)bytes[i + 1] << 8 |
                           (unsigned char)bytes[i + 2];
        out += kAlphabet[v >> 18 & 63];
        out += kAlphabet[v >> 12 & 63];
        out += kAlphabet[v >> 6 & 63];
        out += kAlphabet[v & 63];
    }
    const std::size_t rest = bytes.size() - i;
    if (rest) {
        unsigned v = (unsigned char)bytes[i] << 16;
        if (rest == 2) v |= (unsigned char)bytes[i + 1] << 8;
        out += kAlphabet[v >> 18 & 63];
        out += kAlphabet[v >> 12 & 63];
        out += rest == 2 ? kAlphabet[v >> 6 & 63] : '=';
        out += '=';
    }
    return out;
}

std::string login_header(const Channel& ch, const std::string& login, const std::string& url) {
    if (!ch.login_prefix || !starts_with(ch.login_prefix, "https://")) return std::string();
    if (!starts_with(mirror_urls(url).front(), ch.login_prefix)) return std::string();
    const std::size_t colon = login.find(':');
    if (colon == std::string::npos || colon == 0) return std::string();
    return "Authorization: Basic " + base64(login);
}

void set_process_login(const Channel& ch, const std::string& login) {
    g_login_channel = &ch;
    g_login         = login;
}

std::string process_login_header(const std::string& url) {
    return g_login_channel ? login_header(*g_login_channel, g_login, url) : std::string();
}

}  // namespace ffb
