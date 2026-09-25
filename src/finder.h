// finder.h -- which game is this folder? (docs/SPEC.md, "The flow", step 3.)
//
// Looks in one folder -- the folder itself, no subfolders -- for the exe of
// every games.json entry, without regard to case. The caller turns the three
// outcomes into the flow or one of the error screens (#6).
#pragma once
#include "games.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ffb {

enum class FindOutcome { None, One, Many };

struct FindResult {
    FindOutcome outcome = FindOutcome::None;
    // The entries whose exe is in the folder, in games.json order: empty for
    // None, exactly one for One, two or more for Many.
    std::vector<GameEntry> matches;
};

// A folder that cannot be listed (missing, no access) finds nothing: None.
FindResult find_game(const std::filesystem::path& folder, const std::vector<GameEntry>& games);

}  // namespace ffb
