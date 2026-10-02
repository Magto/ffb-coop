// child_env.cpp -- see child_env.h.
#include "child_env.h"

#include <algorithm>

namespace ffb {
namespace {

// The name part of "NAME=value". A leading '=' belongs to the name, so
// "=C:=C:\x" is named "=C:".
std::wstring name_of(const std::wstring& entry) {
    const size_t eq = entry.find(L'=', 1);
    return eq == std::wstring::npos ? entry : entry.substr(0, eq);
}

// Windows compares environment names case-insensitively and sorts them by
// their upper-case form.
std::wstring upper(std::wstring s) {
    for (wchar_t& c : s)
        if (c >= L'a' && c <= L'z') c = (wchar_t)(c - L'a' + L'A');
    return s;
}

}  // namespace

std::vector<std::wstring> child_environment(const std::vector<std::wstring>& parent,
                                            std::uint64_t steam_appid) {
    if (steam_appid == 0) return parent;
    const std::wstring id = std::to_wstring(steam_appid);
    std::vector<std::wstring> env;
    for (const std::wstring& entry : parent) {
        const std::wstring name = upper(name_of(entry));
        if (name != L"STEAMAPPID" && name != L"STEAMGAMEID") env.push_back(entry);
    }
    env.push_back(L"SteamAppId=" + id);
    env.push_back(L"SteamGameId=" + id);
    std::stable_sort(env.begin(), env.end(), [](const std::wstring& a, const std::wstring& b) {
        return upper(name_of(a)) < upper(name_of(b));
    });
    return env;
}

std::vector<std::wstring> environment_strings(const wchar_t* block) {
    std::vector<std::wstring> out;
    if (!block) return out;
    for (const wchar_t* p = block; *p; p += out.back().size() + 1) out.emplace_back(p);
    return out;
}

std::wstring environment_block(const std::vector<std::wstring>& strings) {
    std::wstring block;
    for (const std::wstring& s : strings) {
        block += s;
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    if (strings.empty()) block.push_back(L'\0');   // an empty block is still two NULs
    return block;
}

}  // namespace ffb
