# Behaviour log

One line per module changed, newest first: the date, the module, what it does differently now, and
the PR.

- 2026-09-25 · games (`src/games.cpp`) · new: reads and checks games.json. One broken rule makes the whole file invalid, and the message names the field (#16)
- 2026-09-25 · plain_name (`src/plain_name.cpp`) · new: the network file-name rule, shared by games.json and the package manifest (#16)
- 2026-09-25 · finder (`src/finder.cpp`) · new: finds none, one or more game exes in the launcher's folder, ignoring case, in the folder itself only (#16)
