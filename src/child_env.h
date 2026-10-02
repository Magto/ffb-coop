// child_env.h -- the environment the package launcher is started with (#28).
//
// A genuine Steam copy of a game calls SteamAPI_RestartAppIfNecessary: started
// outside Steam, the process the package launcher injected exits and Steam
// starts a fresh one without the mod (Magto/mewgenics-coop#627). Steam skips
// that restart when SteamAppId is set for the process, so when games.json
// knows the game's Steam app id the launcher gets SteamAppId and SteamGameId
// set to it, and the game it starts inherits them.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ffb {

// `parent` is this process's environment, one "NAME=value" string each (a
// Windows block may also hold "=C:=C:\..." entries; they pass through).
// -> `parent` unchanged when `steam_appid` is 0. Otherwise `parent` with any
// SteamAppId and SteamGameId removed (names compare case-insensitively, as
// Windows does), "SteamAppId=<id>" and "SteamGameId=<id>" added, and the whole
// list sorted by name the way CreateProcess wants a block sorted.
std::vector<std::wstring> child_environment(const std::vector<std::wstring>& parent,
                                            std::uint64_t steam_appid);

// A Windows environment block ("A=1\0B=2\0\0") <-> its strings.
std::vector<std::wstring> environment_strings(const wchar_t* block);
std::wstring environment_block(const std::vector<std::wstring>& strings);

}  // namespace ffb
