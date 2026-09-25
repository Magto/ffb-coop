# Behaviour log

One line per module changed, newest first: the date, the module, what it does differently now, and
the PR.

- 2026-09-25 · app (`src/app.cpp`, `src/app_win.cpp`, `src/main.cpp`) · new: the start flow. Fetches games.json, self-updates, finds the game, keeps games.json in `FFB Co-op\`, updates the package and starts `FFB Co-op\<launcher> "<game exe>" <every argument>` from `FFB Co-op\`, then exits 0. No game, two games, and offline with nothing installed are error screens that wait for a key and exit 1; unreachable (a 404 too), an invalid games.json or manifest, and a failed package download start the installed package with one warning line. The key wait is skipped when the console is redirected. Self-update, games.json, the finder and the package update are now called from the exe · PR for #6
- 2026-09-25 · selfupdate (new) · given the `launcher` block of games.json, downloads a newer FFB Co-op.exe, checks its size and sha256, moves the running exe aside to `FFB Co-op.old.exe`, puts the new one in place and restarts it with the same command line; a failure at any step warns and carries on as it was, and the restarted process never checks again. Not yet called from the start flow. · PR for #5
- 2026-09-25 · package (`src/package`, `src/net`) · new: validates a game's manifest, fetches only files whose sha256 differs (optional ones too, "Always fetch"), checks size and sha256 as they arrive, replaces nothing when a required file fails; not called from the exe until #6 · PR #17
- 2026-09-25 · games (`src/games.cpp`) · new: reads and checks games.json. One broken rule makes the whole file invalid, and the message names the field (#16)
- 2026-09-25 · plain_name (`src/plain_name.cpp`) · new: the network file-name rule, shared by games.json and the package manifest (#16)
- 2026-09-25 · finder (`src/finder.cpp`) · new: finds none, one or more game exes in the launcher's folder, ignoring case, in the folder itself only (#16)
