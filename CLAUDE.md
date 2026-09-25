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

Lands with #2 (the scaffold).
