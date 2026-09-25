# FFB Co-op.exe — specification

What `FFB Co-op.exe` does, the data it reads, and the decisions Martin took about it. Written from
the agreed design of 2026-09-25 (Martin's brief plus his picks in the lead pane that morning); the
design is agreed, and this file records it rather than reopening it. Where the brief is silent and
this spec had to pick a behaviour, the pick is marked **(spec default)** and listed under
[Defaults this spec chose](#defaults-this-spec-chose); Martin reviewed that list on 2026-09-25,
accepted six and overruled one.

## Why

Players run `mewcoop_loader.exe`, which works only for Mewgenics. A second Steam game is planned
soon. One download, `FFB Co-op.exe`, works out which game it sits next to and fetches the right mod.

## Terms

- **Game folder** — the folder `FFB Co-op.exe` sits in. The player puts it next to the game's exe.
- **Package folder** — the subfolder `FFB Co-op\` inside the game folder. Everything the launcher
  downloads for a game lives there and nowhere else.
- **Package** — the files a game's manifest lists: that game's mod and the launcher that starts it.
- **Package launcher** — the exe inside the package that FFB Co-op.exe starts (for Mewgenics,
  `mewcoop_loader.exe`).

## The flow

The same exe serves every game. There is no Steam lookup and no game menu: the folder decides.

1. **Fetch `https://coopmods.com/games.json`** — once per start. It carries both the launcher's own
   update block and the list of games.
2. **Self-update.** If the `launcher.version` in games.json is newer than the running exe's own
   version resource, download `launcher.url`, check its size and sha256 against the `launcher`
   block, replace the running exe and restart it with the same command line. This ports the
   approach of mewgenics-coop's `loader/mewcoop_update.cpp` (`update_check`: rename the running
   image aside, move the new file into place, relaunch), with its MIT notice and attribution. A
   download that fails its checks replaces nothing, and the current exe carries on with a one-line
   warning **(spec default)**. A restart must never loop: the restarted exe that finds itself
   current does not update again, and one that fails to replace itself carries on as it is.
3. **Find the game.** Look in the game folder for the `exe` of every entry in `games`
   (case-insensitive, the folder itself only, no subfolders).
   - **Exactly one match** — that is the game. Carry on.
   - **None** — the [none screen](#no-game-found).
   - **More than one** — the [two-games screen](#more-than-one-game-found). It should not happen.
4. **Download or update the package** into the package folder from the game's `manifest`, as set
   out under [Package download](#package-download). Uninstalling is deleting `FFB Co-op.exe` and the
   package folder; nothing is written anywhere else.
5. **Start the package launcher** — `FFB Co-op\<launcher>` — with the game exe's full path as its
   first argument, followed by every argument `FFB Co-op.exe` itself was given, unchanged and in
   order. Its working directory is the package folder **(spec default)**. Then FFB Co-op.exe exits
   without waiting for it.

If step 1 fails, the launcher follows [Offline](#offline) instead of steps 2–4.

`FFB Co-op.exe --version`, with `--version` as its only argument, prints the version line and exits
0 before step 1: no network, nothing read or written in the folder. With any other argument beside
it, `--version` is passed on to the package launcher like the rest (#6).

## games.json

Served at `https://coopmods.com/games.json`, UTF-8 JSON, without the site cookie.

### Example

```json
{
  "schema": 1,
  "launcher": {
    "version": "1.0.0",
    "url": "https://coopmods.com/launcher/FFB%20Co-op.exe",
    "sha256": "9f2c4e1a7b3d5f60812a4c6e8b0d2f4a6c8e0b2d4f6a8c0e2b4d6f8a0c2e4b6d",
    "size": 412160
  },
  "games": [
    {
      "id": "mewgenics",
      "name": "Mewgenics",
      "exe": "Mewgenics.exe",
      "steam_appid": 0,
      "manifest": "https://mewgenics.coopmods.com/update/manifest.json",
      "launcher": "mewcoop_loader.exe"
    }
  ]
}
```

The sha256 and size above are illustrative; the publish script (#7) writes the real ones from the
bytes it uploads.

### Validation, field by field

games.json is valid only when every rule below holds. One broken rule makes the whole file
invalid — the launcher never acts on part of a file. An invalid file is handled like an unreachable
server, with its own message ([Offline](#offline)) **(spec default)**.

| Field | Type | Rule |
|---|---|---|
| (top level) | object | Must parse as JSON and be an object. Unknown keys, here and in every object below, are ignored, so a later schema can add fields without breaking older launchers. |
| `schema` | integer | Required. Must be `1`. Any other value is invalid: the file is in a format this launcher does not know. |
| `launcher` | object | Required. |
| `launcher.version` | string | Required. `MAJOR.MINOR.PATCH`, each part a non-negative decimal integer without a sign; compared part by part as numbers (`1.10.0` is newer than `1.9.0`). |
| `launcher.url` | string | Required. An absolute `https://` URL. |
| `launcher.sha256` | string | Required. Exactly 64 hex digits; compared without regard to case. The downloaded bytes must hash to it. |
| `launcher.size` | integer | Required. At least 1. The download must be exactly this many bytes. |
| `games` | array | Required. At least one entry. |
| `games[].id` | string | Required. 1–32 characters from `a-z`, `0-9` and `-`. Unique across `games`. |
| `games[].name` | string | Required. Not empty. Shown to the player; never used as a path. |
| `games[].exe` | string | Required. A [plain file name](#network-file-name-rule) ending in `.exe` (any case). Unique across `games`, compared without regard to case. |
| `games[].steam_appid` | integer | Required. At least 0; `0` means not known yet. Data only: the launcher reads it and does nothing with it. |
| `games[].manifest` | string | Required. An absolute `https://` URL. |
| `games[].launcher` | string | Required. A [plain file name](#network-file-name-rule) ending in `.exe`. Must also be the `name` of one of that manifest's files, marked required — checked once the manifest is fetched. |

### The game manifest

`games[].manifest` points at a per-game update manifest in the format the Mewgenics loader already
reads:

```json
{ "version": "76.0.0", "wire": 34,
  "files": [ { "name": "mewcoop.dll", "size": 4194304, "sha256": "…", "required": true } ] }
```

(Values illustrative.)

Mewgenics already publishes it (mewgenics-coop `tools/ship.py` runs `tools/publish_site.py`),
cookie-free, so Mewgenics needs no new server paths. Each file is downloaded from the manifest's
own URL with the last path segment replaced by the file's name — the rule mewgenics-coop's loader
uses (`sibling_path`), so `…/update/manifest.json` gives `…/update/mewcoop.dll`.

| Field | Rule |
|---|---|
| `version` | Required string, `N` or `X.Y.Z`. Shown to the player; the launcher does not compare it (see Package download). |
| `wire` | Read by the Mewgenics mod, not by FFB Co-op.exe. Ignored. |
| `files` | Required array, at least one entry. |
| `files[].name` | Required. A [plain file name](#network-file-name-rule). Unique within `files`, compared without regard to case. |
| `files[].size` | Required integer, at least 1. |
| `files[].sha256` | Required. Exactly 64 hex digits, compared without regard to case. |
| `files[].required` | Optional boolean, default `true`. |

A manifest that breaks any rule is rejected whole, and nothing is downloaded from it.

## Package download

1. For each file in the manifest, hash the copy in the package folder if there is one. A file whose
   sha256 already matches is left alone **(spec default: every file is compared by hash; the
   manifest's `version` decides nothing)**.
2. A file marked `"required": false` is kept present and current exactly like a required one: it
   is fetched on a fresh install too (Martin's pick: "Always fetch"). Only a failure is treated
   differently — see step 3. This **differs on purpose** from the Mewgenics loader's own rule, which
   never fetches `mewcoop_ui.swf` into an install that lacks it: every player who switches to
   FFB Co-op.exe starts with an empty package folder ("Start fresh"), so under the loader's rule
   nobody installing through FFB Co-op.exe would ever get the optional files.
3. Every file that differs is downloaded to `<name>.new` in the package folder, checking size and
   sha256 as it arrives. A required file that fails either check is deleted, and **nothing** is
   replaced. An optional file that fails is deleted and dropped from this run with a one-line
   warning naming it; the required files are still replaced, and the start is not blocked.
4. Only once every file has arrived and checked are the `.new` files moved over the old ones
   (replace in place). A file that cannot be replaced — usually because the game is running and
   holds it open — leaves the old file where it was and says which file and why.
5. After a failed download or replace, the launcher continues as in [Offline](#offline): it starts
   the installed package with a one-line warning if every required file is present, and otherwise
   shows the error and waits for a key **(spec default)**.

A good games.json is also saved as `FFB Co-op\games.json` once the game is found, so a later
offline start knows which exe names to look for **(spec default)**.

## Network file-name rule

Every name that arrives over the network and becomes a file name — `games[].exe`,
`games[].launcher`, `files[].name` — must be a **plain file name**:

- not empty, and at most 255 characters;
- no `/`, `\` or `:`, none of `<` `>` `"` `|` `?` `*`, and no control character (U+0000–U+001F
  or U+007F);
- not `.` or `..`, and no `..` anywhere in it;
- no leading or trailing space and no trailing `.` (Windows strips them silently);
- not a Windows reserved device name (`CON`, `PRN`, `AUX`, `NUL`, `CONIN$`, `CONOUT$`, `CLOCK$`,
  `COM1`–`COM9`, `LPT1`–`LPT9`, and `COM¹`–`COM³`, `LPT¹`–`LPT³` with superscript digits), in
  any case, with or without an extension, and with or without spaces before the extension
  (`nul .txt` is the device too).

A name that breaks the rule makes the file that carries it invalid (games.json or the manifest),
and the launcher says which name it rejected. Downloaded files are only ever written inside the
package folder; nothing from the network is ever written beside the game exe or anywhere else.

## Error screens

Each screen is console text. Where it says "waits for a key", it prints `Press any key to exit.`
and waits, so a double-clicked console window does not vanish before the player reads it.

### No game found

```
Could not find a FFB modded game in <game folder>
Looking for: Mewgenics.exe
Put FFB Co-op.exe next to the game's exe and start it again.
```

The `Looking for:` line lists every `games[].exe`, comma-separated. Waits for a key, exits 1.

### More than one game found

```
Found more than one FFB modded game in <game folder>: <exe>, <exe>
FFB Co-op.exe can only serve one game per folder.
```

Waits for a key, exits 1. It should not happen.

### Offline

Martin's pick: "Run installed, warn (Recommended)".

- **Server unreachable, package installed** — the game is found from `FFB Co-op\games.json`, every
  required file of the package is present, so the launcher prints one line and starts it:
  `Could not reach coopmods.com -- starting the installed version.` No key press.
- **Server unreachable, no package yet** (no `FFB Co-op\games.json`, or a required file missing):
  `Could not reach coopmods.com, and FFB Co-op is not installed in <game folder> yet. Connect to the
  internet and start it again.` Waits for a key, exits 1.
- **games.json or the manifest invalid** — the same two cases, with
  `coopmods.com sent a file this version cannot read (<reason>)` in place of `Could not reach
  coopmods.com`.

## The Mewgenics package

Today's `mewcoop_loader.exe`, `mewcoop.dll` and `mewcoop_ui.swf`, under the same names. **Never
rename them**: every installed loader (v72 and older) accepts only those three names from the
manifest (the name allowlist in `update_check`, mewgenics-coop `loader/mewcoop_update.cpp`), and
the loader finds `mewcoop.dll` by name next to itself (`loader/mewcoop_loader.cpp`).

- The loader already takes the game path as its first argument, so it may need no change; a hand
  test from `FFB Co-op\` confirms it (the mewgenics-coop v76 item under [Issue cut](#issue-cut)).
- The mod reads `mewcoop.json` beside `mewcoop.dll` (`src/core/mewcoop_config.cpp`), that is, in
  `FFB Co-op\`.
- The loader keeps self-updating from the same manifest; started by FFB Co-op.exe right after a
  package update, that is a no-op.
- **No settings carry-over** (Martin's pick: "Start fresh"). A player switching from the standalone
  loader sets up `FFB Co-op\mewcoop.json` again, and the Mewgenics changelog must say so.

The second game's package brings its own launcher. Whether it uses a mod API or injection is not
known yet, and FFB Co-op.exe assumes neither.

## Server layout

| What | Where |
|---|---|
| Host | Hetzner, `89.167.37.21`; `coopmods.com` already resolves there and serves a placeholder |
| Web server | Caddy, container `caddy`, Caddyfile `/opt/matrix/caddy/Caddyfile` |
| `https://coopmods.com/games.json` | games.json — served **without** the cookie |
| `https://coopmods.com/launcher/FFB%20Co-op.exe` | the launcher's own update — served **without** the cookie |
| `https://mewgenics.coopmods.com/update/manifest.json` and its sibling files | the Mewgenics package, already published by mewgenics-coop, already cookie-free |

The Caddyfile is a single-file bind mount: **never `sed -i` it** (that replaces the inode and the
container keeps the old file). Edit it in place — open read/write, write, truncate — then
`docker exec caddy caddy reload --config /etc/caddy/Caddyfile`. The cookie gate and its exceptions
work as in mewgenics-coop's `site/README.md`: each cookie-free path is named in the site block.
The publish script and the coopmods.com site block are #7.

## Constraints

- Existing players' standalone loader keeps working; there is no forced migration.
- The file is `FFB Co-op.exe`. Its version resource has FileDescription and ProductName
  "FFB Co-op". Its icon is the navy bunnycorn, mewgenics-coop `res/Mewcoop.ico` (commit 0246ab7).
- Windows only, MSVC via CMake, like mewgenics-coop; it builds on Tubal-Cain.

## Testing

**Unit** (tier `code`): the folder finder and the update code as plain functions — an empty folder,
one match, two matches, a malformed games.json, a package file with a bad sha256, a network file
name with a path in it. Every validation rule above is a case.

**By hand** (tier `manual`, Windows): in the Mewgenics folder it downloads the package and the game
starts with co-op; in an empty folder the "Could not find" message stays until a key press; offline
with a package installed it starts with the warning.

## Issue cut

In `Magto/ffb-coop`, in this order: #2 first; then #3, #4 and #5 in parallel; then #6; then #7.

| # | What |
|---|---|
| #1 | This spec and the process adoption |
| #2 | Scaffold: CMake console exe `FFB Co-op.exe`, version resource and icon, a unit-test target, CI |
| #3 | games.json parser and game-folder finder, with tests |
| #4 | Package download into `FFB Co-op\` from a game manifest (sha256, safe replace, plain-name check), with tests |
| #5 | Self-update of FFB Co-op.exe (port from mewgenics-coop's update code, MIT notice and attribution) |
| #6 | Start the package launcher; the none, two-games and offline screens |
| #7 | Server: the coopmods.com block in Caddy serving `/games.json` and `/launcher/` cookie-free; the publish script |

In `Magto/mewgenics-coop`, with v76: hand-test the loader from `FFB Co-op\` with the path argument;
a changelog line telling players to switch (and that settings start fresh); a download link for
FFB Co-op.exe on the site. A loader fix only if the test shows one is needed.

## Decisions

Martin's picks, 2026-09-25, in the lead pane, quoted exactly:

- **Repo:** "Magto/ffb-coop, own board (Recommended)"
- **Settings:** "Start fresh"
- **Offline:** "Run installed, warn (Recommended)"

## Defaults this spec chose

The brief does not settle these; the spec picked the behaviour marked **(spec default)** above.
Martin reviewed the list on 2026-09-25 (recorded on PR #13): he accepted six — "Accept all six
(Recommended)" — and overruled number 5 with "Always fetch".

1. A self-update that fails its checks is not fatal: the running exe carries on with a warning. **Accepted.**
2. An invalid games.json or manifest is handled like an unreachable server, with its own message. **Accepted.**
3. A good games.json is kept as `FFB Co-op\games.json`, so an offline start can find the game. **Accepted.**
4. Package files are compared by sha256 one by one; the manifest's `version` decides nothing. **Accepted.**
5. ~~A `"required": false` file is only updated, never fetched fresh (the Mewgenics loader's rule).~~
   **Overruled — Martin's decision:** "Always fetch". Optional files are kept present and current
   like required ones, fetched on a fresh install too; a failed optional download warns and does
   not block the start (Package download, steps 2–3).
6. A failed download or replace falls back to the offline behaviour. **Accepted.**
7. The package launcher starts with the package folder as its working directory. **Accepted.**
