// channel.cpp -- see channel.h.
#include "channel.h"

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
    "https://coopmods.com/dev/games.json",
    "FFB Co-op dev",
    "https://coopmods.com/dev/",
    "MEWCOOP_NOUPDATE=1",
};

const Channel* g_login_channel = nullptr;
std::string    g_login;

bool starts_with(const std::string& s, const char* prefix) {
    const std::string p(prefix);
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

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
    if (!starts_with(url, ch.login_prefix)) return std::string();
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
