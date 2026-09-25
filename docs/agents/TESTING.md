# TESTING — how a claim is proven here

Two tiers, lowest first: **`code`** and **`manual`** (`TEST_TIERS` in `LEAD-CONFIG.md`). There is
no `loopback` tier: FFB Co-op.exe runs alone, with no second peer to agree with.

## `code` — unit tests

The build and test commands are in `CLAUDE.md` under `## Build and test` and in `LEAD-CONFIG.md`
(`BUILD_CMD`, `TEST_CMD`, `TEST_LIST_CMD`). The project builds on Windows only (MSVC via CMake);
WSL cannot build it, so a worker in WSL writes `revert check pending` and `CODE-ONLY` and the
Windows runner (`MODULE_WINDOWS_RUNNER`, build host Tubal-Cain) proves it.

What `docs/SPEC.md` asks to be unit-tested, as plain functions with no network and no real game
folder: the folder finder (empty folder, one match, two matches), the games.json parser (every
validation rule, malformed input), the package update (a bad sha256, a network file name with a
path in it).

The revert check: restore `src/` only from `origin/main`, rebuild, and the named test must fail.
Never bulk-replace `docs/` or `tools/` — the gates live there.

## `manual` — by hand on Windows

For what only a real game folder and a real network show: the package download in the Mewgenics
folder, the "Could not find" screen in an empty folder, the offline start with a package installed.
A manual plan is a numbered script for Martin — set-up, action, what he sees, and the console line
that proves it — and says why `code` cannot prove the claim.
