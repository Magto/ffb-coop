# Behaviour log

One line per module changed, newest first: the date, the module, what it does differently now, and
the PR.

- 2026-09-25 · selfupdate (new) · given the `launcher` block of games.json, downloads a newer FFB Co-op.exe, checks its size and sha256, moves the running exe aside to `FFB Co-op.old.exe`, puts the new one in place and restarts it with the same command line; a failure at any step warns and carries on as it was, and the restarted process never checks again. Not yet called from the start flow. · PR for #5
