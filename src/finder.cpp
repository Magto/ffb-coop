// finder.cpp -- see finder.h.
#include "finder.h"

#include "plain_name.h"

#include <set>
#include <system_error>

namespace ffb {

FindResult find_game(const std::filesystem::path& folder, const std::vector<GameEntry>& games) {
    namespace fs = std::filesystem;

    // The names of the plain files in the folder, lower-cased. A directory
    // called Mewgenics.exe is not the game.
    std::set<std::string> present;
    std::error_code ec;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code type_ec;
        if (!it->is_regular_file(type_ec)) continue;
        present.insert(ascii_lower(it->path().filename().u8string()));
    }

    FindResult r;
    for (const GameEntry& g : games)
        if (present.count(ascii_lower(g.exe))) r.matches.push_back(g);
    r.outcome = r.matches.empty()     ? FindOutcome::None
                : r.matches.size() == 1 ? FindOutcome::One
                                        : FindOutcome::Many;
    return r;
}

}  // namespace ffb
