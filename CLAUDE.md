# FFB Co-op

One Windows launcher, `FFB Co-op.exe`, that sits in a game's folder, works out which co-op-modded
game it is next to, and downloads and starts that game's mod. What it does, and Martin's decisions
about it, are in `docs/SPEC.md`.

## Before anything else

This repo runs on the agent-workflow process (`Magto/agent-workflow`). Read
`docs/agents/START.md` first, then the issue you were given; every project fact the process needs
(board, labels, commands, models) is in `docs/agents/LEAD-CONFIG.md`. A value that is not there is
a stop, not a guess.

Run `tools/doc_rules.sh` and `tools/scan_rules.sh` before you push; both must exit 0.
`tools/install_hooks.sh` installs a pre-push hook that runs them.

## Build and test

Windows only (MSVC via CMake, same toolchain as mewgenics-coop); WSL cannot build it.

- Configure: `cmake -B build -A x64`
- Build: `cmake --build build --config Release` → `build\Release\FFB Co-op.exe` and `FFB Co-op - dev.exe`
- The dev exe needs the shared dev password at build time, never committed: `FFB_DEV_PASSWORD`, or
  `FFB_DEV_PASSWORD_FILE` naming a file, or `~/.config/coopmods/ffb-dev-password` (the lead's copy, WSL side; from
  WSL pass it with `FFB_DEV_PASSWORD_FILE=$HOME/.config/coopmods/ffb-dev-password WSLENV=FFB_DEV_PASSWORD_FILE/p`).
  Without one the build fails; `cmake -B build -A x64 -DFFB_BUILD_DEV=OFF` builds the public exe alone.
- Unit tests: `ctest --test-dir build -C Release --output-on-failure`
- List registered tests: `ctest --test-dir build -C Release -N`

The version lives in `src/ffb_version.h` only; `res/ffb_coop.rc` and the printed version line both read it.
New tests: one `tests/test_<name>.cpp` using `tests/ffb_test.h`, registered in `tests/CMakeLists.txt` with `add_test`.
