# Behaviour log

One line per module changed, newest first: the date, the module, what it does differently now, and
the PR.

- 2026-09-25 · package (`src/package`, `src/net`) · new: validates a game's manifest, fetches only files whose sha256 differs (optional ones too, "Always fetch"), checks size and sha256 as they arrive, replaces nothing when a required file fails; not called from the exe until #6 · PR #17
