# Behaviour log

One line per module changed, newest first: the date, the module, what it does differently now, and
the PR.

- 2026-09-25 · publish (`tools/publish.py`, host-side SHIP_CMD) · new: writes games.json from the built exe and `site/games.json.in`, checks it against the SPEC rules (the file-name rule as #16 wrote it), and uploads the exe then games.json to coopmods.com under temporary names; refuses on a failing gate, a version mismatch or a live launcher it would not replace. Has not published yet · PR #14
- 2026-09-25 · selfupdate (new) · given the `launcher` block of games.json, downloads a newer FFB Co-op.exe, checks its size and sha256, moves the running exe aside to `FFB Co-op.old.exe`, puts the new one in place and restarts it with the same command line; a failure at any step warns and carries on as it was, and the restarted process never checks again. Not yet called from the start flow. · PR for #5
- 2026-09-25 · package (`src/package`, `src/net`) · new: validates a game's manifest, fetches only files whose sha256 differs (optional ones too, "Always fetch"), checks size and sha256 as they arrive, replaces nothing when a required file fails; not called from the exe until #6 · PR #17
- 2026-09-25 · games (`src/games.cpp`) · new: reads and checks games.json. One broken rule makes the whole file invalid, and the message names the field (#16)
- 2026-09-25 · plain_name (`src/plain_name.cpp`) · new: the network file-name rule, shared by games.json and the package manifest (#16)
- 2026-09-25 · finder (`src/finder.cpp`) · new: finds none, one or more game exes in the launcher's folder, ignoring case, in the folder itself only (#16)
