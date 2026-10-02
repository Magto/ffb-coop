// test_child_env.cpp -- the package launcher's environment (#28): SteamAppId and
// SteamGameId set to games.json's steam_appid when it is above 0, so a genuine
// Steam copy does not restart itself without the mod; the parent's environment
// otherwise passed through.
#include "child_env.h"
#include "ffb_test.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {

using Env = std::vector<std::wstring>;

bool has(const Env& env, const std::wstring& entry) {
    return std::find(env.begin(), env.end(), entry) != env.end();
}

size_t count_named(const Env& env, const std::wstring& upper_name) {
    size_t n = 0;
    for (const std::wstring& e : env) {
        std::wstring name = e.substr(0, e.find(L'=', 1));
        for (wchar_t& c : name)
            if (c >= L'a' && c <= L'z') c = (wchar_t)(c - L'a' + L'A');
        if (name == upper_name) ++n;
    }
    return n;
}

// A parent environment as GetEnvironmentStringsW gives it: the "=C:" drive
// entries first, then sorted by name.
const Env kParent = {L"=C:=C:\\Games", L"ALLUSERSPROFILE=C:\\ProgramData", L"Path=C:\\Windows;C:\\Tools",
                     L"TEMP=C:\\Temp", L"windir=C:\\Windows"};

void test_zero_passes_parent_through() {
    std::printf("steam_appid 0: the parent's environment unchanged, no SteamAppId\n");
    const Env env = ffb::child_environment(kParent, 0);
    CHECK(env == kParent);
    CHECK(count_named(env, L"STEAMAPPID") == 0);
    CHECK(count_named(env, L"STEAMGAMEID") == 0);
}

void test_id_sets_both() {
    std::printf("steam_appid 686060 (Mewgenics): SteamAppId and SteamGameId set, the rest passed through\n");
    const Env env = ffb::child_environment(kParent, 686060);
    CHECK(has(env, L"SteamAppId=686060"));
    CHECK(has(env, L"SteamGameId=686060"));
    CHECK(env.size() == kParent.size() + 2);
    for (const std::wstring& e : kParent) CHECK(has(env, e));
}

void test_id_replaces_existing() {
    std::printf("an inherited SteamAppId or steamgameid is replaced, never doubled\n");
    Env parent = kParent;
    parent.push_back(L"SteamAppId=480");
    parent.push_back(L"steamgameid=480");
    const Env env = ffb::child_environment(parent, 686060);
    CHECK(count_named(env, L"STEAMAPPID") == 1);
    CHECK(count_named(env, L"STEAMGAMEID") == 1);
    CHECK(has(env, L"SteamAppId=686060"));
    CHECK(has(env, L"SteamGameId=686060"));
    CHECK(!has(env, L"SteamAppId=480"));
    CHECK(!has(env, L"steamgameid=480"));
}

void test_sorted_for_create_process() {
    std::printf("the block stays sorted by upper-case name, the =C: entry first\n");
    const Env env = ffb::child_environment(kParent, 686060);
    const Env want = {L"=C:=C:\\Games", L"ALLUSERSPROFILE=C:\\ProgramData", L"Path=C:\\Windows;C:\\Tools",
                      L"SteamAppId=686060", L"SteamGameId=686060", L"TEMP=C:\\Temp", L"windir=C:\\Windows"};
    CHECK(env == want);
}

void test_block_round_trip() {
    std::printf("a Windows block and its strings convert both ways\n");
    const wchar_t block[] = L"A=1\0B=two\0=C:=C:\\\0\0";
    const Env strings = ffb::environment_strings(block);
    CHECK((strings == Env{L"A=1", L"B=two", L"=C:=C:\\"}));
    const std::wstring back = ffb::environment_block(strings);
    CHECK(back == std::wstring(block, sizeof(block) / sizeof(wchar_t) - 1));
    CHECK(ffb::environment_strings(nullptr).empty());
    CHECK(ffb::environment_block(Env{}) == std::wstring(2, L'\0'));
}

}  // namespace

int main() {
    test_zero_passes_parent_through();
    test_id_sets_both();
    test_id_replaces_existing();
    test_sorted_for_create_process();
    test_block_round_trip();
    return ffb_test_result();
}
