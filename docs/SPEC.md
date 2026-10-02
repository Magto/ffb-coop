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
   update block and the list of games. Its signature, `games.json.sig`, is fetched beside it and
   checked before a byte of games.json is read ([Signatures](#signatures), #21); a missing or wrong
   signature is handled like an invalid file, so there is no self-update.
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
   order. Its working directory is the package folder **(spec default)**. When the game's
   `steam_appid` is above 0, the package launcher is started with `SteamAppId` and `SteamGameId` set
   to it in its environment, and the game it starts inherits them; otherwise it gets FFB Co-op.exe's
   environment unchanged (#28). A genuine Steam copy started outside Steam restarts itself through
   Steam, and the restarted game has no mod; with `SteamAppId` set it does not restart
   (mewgenics-coop#627). Then FFB Co-op.exe exits without waiting for it.

If step 1 fails — no answer, or a games.json that is unsigned, badly signed or invalid — the launcher
follows [Offline](#offline) instead of steps 2–4.

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
      "steam_appid": 686060,
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
| `games[].steam_appid` | integer | Required. At least 0; `0` means not known yet. Above 0, the package launcher is started with `SteamAppId` and `SteamGameId` set to it ([The flow](#the-flow), step 5); `0` leaves its environment as it is. |
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

A manifest that breaks any rule is rejected whole, and nothing is downloaded from it. Before any of
those rules, its signature `<manifest URL>.sig` must check out ([Signatures](#signatures)).

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

A good (signed and valid) games.json is also saved as `FFB Co-op\games.json` once the game is found, so a later
offline start knows which exe names to look for **(spec default)**. After a good update the game's
manifest is kept the same way, as `FFB Co-op\manifest.json`, so an offline start knows every
required file of the package (#6).

## Signatures

Added by #21. The sha256 values in games.json and in a manifest catch a broken download, not a
hostile server: whoever controls coopmods.com, its web server or its DNS could publish new bytes and
a matching hash. So both files are signed offline, with a key that is in no repo and not on the
server, and FFB Co-op.exe checks the signature before it reads the file. games.json carries the
launcher's own sha256, and the manifest the sha256 of every package file, so the signatures cover the
exe and the package too.

### Format

- **Algorithm:** ed25519 (RFC 8032), raw: no wrapper format, no key id.
- **Signature file:** `<file>.sig`, served beside the file — `https://coopmods.com/games.json.sig`,
  and for a manifest its URL with `.sig` appended (a query or fragment dropped first):
  `https://mewgenics.coopmods.com/update/manifest.json.sig`.
- **Content:** the 64-byte signature over the **exact bytes** of the file as served, written as 128
  hex digits (the publish side writes lowercase and a newline; the launcher accepts either case and
  trailing whitespace, nothing else).
- **The signed file is unchanged.** games.json and the manifest keep their format, so a launcher or
  loader that does not check signatures reads them as before.
- The same format signs mewgenics-coop's update manifest (Magto/mewgenics-coop#553), with the same
  key.

### What the launcher does

| Case | games.json | A game's manifest |
|---|---|---|
| `.sig` signs the file under a trusted key | read and acted on as today | read and acted on as today |
| `.sig` missing (any HTTP status but 200), not 128 hex digits, signed by another key, or the file changed by even one byte | refused: one warning line, **no self-update**, the installed version starts ([Offline](#offline)) | refused: one warning line, nothing downloaded, **no file in the game folder changes** (not even the kept games.json), the installed version starts |
| `.sig` gets no answer at all (transport failure) | as unreachable | as unreachable |

The warning reads `coopmods.com sent a file without a valid signature (<reason>) -- starting the
installed version.`, where `<reason>` is one of `games.json is not signed: <sig URL> said HTTP <n>`,
`games.json: the signature is not 128 hex digits`, `games.json: the signature does not match`, or the
same three with `the manifest` for `games.json`. With nothing installed it is the not-installed
screen with that prefix ([Offline](#offline)).

The copies kept in `FFB Co-op\` for offline starts are written only after their signature checked,
and are not checked again: they are on the player's own disk.

### Where the keys live

- **Public key:** compiled into FFB Co-op.exe from `src/trusted_keys.h`, one 64-hex-digit key per
  line. A file signed by **any** key in that list is accepted. Today it holds one key, made
  2026-10-01: `43706304f0fae090f7a2afd96e06d207ab193ea52a527d6576073ce281b30a59`.
- **Private key:** 32 raw bytes (the ed25519 seed) in `~/.config/coopmods/signing.key` on
  Tubal-Cain's WSL side, mode 600 in a mode-700 folder, with an offline backup in Martin's password
  manager (Martin's decision, 2026-10-01: "yes, I like your suggestion where it should live"). It is
  in no repo and never on the server. `COOPMODS_SIGNING_KEY` names another path (for example to run
  the publish from Windows Python).
- **Signing:** `tools/signing.py` (needs Python `cryptography`). `tools/publish.py` signs games.json
  with it and refuses to publish without the key, or with a key whose public key is not in
  `src/trusted_keys.h`. `python tools/signing.py sign <file>` writes `<file>.sig` for anything else,
  such as a manifest; `verify` checks one.

### Rotation

1. Make the new key pair. Add its public key to `src/trusted_keys.h` **beside** the old one, and
   publish that launcher, signed with the **old** key. Every launcher in the field updates to it and
   now trusts both.
2. Wait until the launchers that matter have updated (the old key keeps signing meanwhile).
3. Switch `~/.config/coopmods/signing.key` to the new key; re-sign games.json and every manifest.
4. In a later launcher, drop the old key from `src/trusted_keys.h`.

A launcher only ever learns a key by self-updating, so a launcher that skips step 1 (too old, never
updated) refuses everything signed by the new key from step 3 on and keeps starting its installed
version; it needs a fresh download of FFB Co-op.exe. If the private key leaks, the attacker can sign
anything the launcher trusts until step 4 ships, so a leak means doing all four steps at once and
telling players to download FFB Co-op.exe again.

### Transition

**A launcher without the check is already in the field.** FFB Co-op.exe 0.1.0, built before #21,
has been served from coopmods.com since 2026-09-25: `games.json` advertises launcher 0.1.0
(586752 bytes) and `launcher/FFB%20Co-op.exe` answers it (observed by the #22 review, 2026-10-01
23:25). Players holding it must reach the first signing version by self-update, and this is how:

- **v1 (`1.0.0`) is the first signing version** (#24). A launcher compares versions only, so a
  signing build still numbered 0.1.0 would never reach a 0.1.0 player; 0.1.0 stays the unnumbered
  preview, and v1 is the first public version. `tools/publish.py --release 1` refuses an exe that is
  not 1.0.0, and its comparison with the live games.json refuses a lower version and the same
  version with different bytes ("bump the version").
- **0.1.0 never asks for a signature.** games.json keeps its format and the signature is a separate
  file, so 0.1.0 reads a signed games.json exactly as an unsigned one.

The release order, and what a 0.1.0 launcher and a signing launcher see at each step:

| Step | A 0.1.0 launcher | A signing launcher |
|---|---|---|
| 1. The coopmods.com site block serves `/games.json.sig` without a cookie (`site/README.md`). Until it does, `tools/publish.py` refuses to upload. | No change: it never fetches the `.sig`. | (none exists yet) |
| 2. mewgenics-coop publishes `manifest.json.sig` beside its manifest, signed with this key (Magto/mewgenics-coop#553). | No change: it never fetches the `.sig`. | (none exists yet) |
| 3. `tools/publish.py --release 1` publishes the signing FFB Co-op.exe as v1 (`1.0.0`): the exe, then `games.json.sig`, then games.json. | On its next start it reads the new games.json, sees the higher version, downloads the exe, checks its size and sha256, replaces itself and restarts as the signing version. Before games.json is renamed it still sees the old games.json and stays as it is, to update on the next start. | From then on: games.json and the Mewgenics manifest are checked and accepted, and the flow is as before. |

If step 3 ran before step 2, the 0.1.0 launchers would still update in step 3, but the signing
version they became would refuse the unsigned Mewgenics manifest. An installed player would get the
old mod with the warning on every start, and a new player the not-installed screen, until step 2.
Step 2 therefore comes first.

**The Mewgenics loader** (`mewcoop_loader.exe`) reads the same manifest and ignores the `.sig`
beside it until mewgenics-coop#553 teaches it to check.

### Not covered

- **Replaying an older signed file.** A server can serve an older games.json or manifest with its
  genuine signature. Self-update never goes to a lower version, but an older package would install.
- **The launcher's own exe** is not Authenticode-signed (SmartScreen); that is a separate issue.

## Dev channel

Added by #26, for trying a build before it reaches every player. Martin, 2026-10-02 20:05 (mewgenics-coop lead
pane): "do a seperate self-updating dev chanel that gets the exe from a passworded site on coopmods.com. And it will
be me and budda to start with. … I want it to update flawlessly like the real ffb coop does. Could they live in the
same folder and the dev files just ends up in ~/ffb coop dev/ ?"

### One source, two exes

`FFB Co-op - dev.exe` is the same source as `FFB Co-op.exe`, built with one switch (`FFB_DEV_CHANNEL`, the
`ffb_coop_dev` target). It is opt-in (Martin, 2026-10-02 21:51, "Opt-in (Recommended) — FFB_BUILD_DEV defaults OFF; only a
dev build (-DFFB_BUILD_DEV=ON) needs the password. Plain build and release exe never touch the secret."): configured
with `-DFFB_BUILD_DEV=ON`, `cmake --build build --config Release` makes both; a plain build makes `FFB Co-op.exe`
alone. The switch changes only the values in
this table (`src/channel.cpp`) and the names and version in `src/ffb_version.h`; the flow, the signature check, the
self-update and the package update are the same code.

| | `FFB Co-op.exe` | `FFB Co-op - dev.exe` |
|---|---|---|
| games.json | `https://coopmods.com/games.json` | `https://coopmods.com/dev/games.json` |
| Its own update | `https://coopmods.com/launcher/FFB%20Co-op.exe` | `https://coopmods.com/dev/launcher/FFB%20Co-op%20-%20dev.exe` |
| A game's manifest | whatever games.json names (Mewgenics: `https://mewgenics.coopmods.com/update/manifest.json`) | whatever the dev games.json names (Mewgenics: `https://coopmods.com/dev/mewgenics/manifest.json`) |
| Package folder | `<game folder>\FFB Co-op\` | `<game folder>\FFB Co-op dev\` |
| Password | none | the shared dev password, below |
| Version | `N.0.0` (vN) | `N.0.D`: dev build D, D at least 1 (`FFB_DEV_BUILD`) |
| Title and first line | `FFB Co-op vN`, `FFB Co-op N.0.0` | `FFB Co-op - dev vN`, `FFB Co-op - dev N.0.D` |

### Side by side in one game folder

Both exes sit next to the game's exe. Martin asked for "~/ffb coop dev/"; the folder is `FFB Co-op dev\` inside the
game folder, beside `FFB Co-op\`, as the issue's first Done bullet puts it. The dev exe reads and writes only
`FFB Co-op dev\` (the package and the kept games.json and manifest.json) and its own
`FFB Co-op - dev.exe.new` / `FFB Co-op - dev.old.exe` during a self-update. It never reads or writes `FFB Co-op\`:
offline with only the public package installed it says it is not installed rather than start the public one. The
public exe never fetches a URL under `https://coopmods.com/dev/` and never reads `FFB Co-op dev\`;
`tools/publish.py` refuses a public games.json that points under `/dev/`.

### The same rules

Everything above holds for the dev exe with the dev values: games.json and every manifest checked against the same
trusted keys before a byte is read, signed with the same key by the same `tools/signing.py` (no paid certificate);
the same validation; self-update only to a strictly newer version, with the size and sha256 checked, through the
`.new` swap, restarting once; offline, unsigned or invalid starts the installed dev package with one warning, or
shows the not-installed screen. The error screens name `FFB Co-op - dev.exe` where the public ones name
`FFB Co-op.exe`.

The dev exe sets `MEWCOOP_NOUPDATE=1` in the environment the package launcher inherits. The dev exe is the package's
updater; the Mewgenics loader's own update reads the public manifest and would put the public build back over the
dev one (`MEWCOOP_NOUPDATE` is the loader's existing switch for that, mewgenics-coop `loader/mewcoop_loader.cpp`).

### The dev password

Martin, 2026-10-02 21:28 (lead pane, Decision comment on #26): "Lets do no login or if we can only use a password",
then picked **"One shared password"**: "No user name and no prompt: the shared password is built into the dev exe and
checked by the server for /dev/." This replaced the per-person login first proposed here.

- **One credential.** Everything under `https://coopmods.com/dev/` is behind HTTP Basic auth with the one user name
  `dev` and the shared password, checked by Caddy's `basic_auth` against a bcrypt hash in the site block
  (site/README.md, "The dev channel"). The dev exe never asks for anything and stores nothing.
- **Built in at build time only.** The password is never in the repo, an issue, a PR or a log. The dev exe's build
  reads it from outside the repo (`FFB_DEV_PASSWORD`, the file named by `FFB_DEV_PASSWORD_FILE`, or
  `~/.config/coopmods/ffb-dev-password`, where the lead keeps it) into a header in the build tree; with none, the
  build fails rather than make a dev exe with no credential. Only a dev build (`-DFFB_BUILD_DEV=ON`) reads it; the
  plain build and the public exe have no password at all.
- **Every fetch** of a URL that starts with `https://coopmods.com/dev/` carries `Authorization: Basic …`, and only
  those: never the public URLs, never another host, never plain http, and never through a redirect.
- **A refused password** (games.json answers HTTP 401): the installed dev package starts with `coopmods.com refused
  the dev password (HTTP 401) -- starting the installed version.`, or the not-installed screen with that prefix.
- **What it costs:** the password sits in every copy of the dev exe, so anyone holding the exe can read it out.
  There is no per-person revoke; a leaked password is replaced, and every dev exe already out there then needs the
  new one dropped in by hand once.

Getting the first copy: Martin hands it over, or download `https://coopmods.com/dev/launcher/FFB%20Co-op%20-%20dev.exe`
in a browser (user `dev`, the shared password); from then on it updates itself.

### Publishing

`python tools/publish.py --dev` (site/README.md, "The dev channel"). It puts the dev exe and/or a dev package under
`/opt/downloads/ffb-coop-site/dev/` and nowhere else, signed with the same key, and refuses an exe that is not a dev
version (so a release exe never lands on the dev channel, and `--release` refuses a dev exe), a version lower than the
live dev one, and any upload while `https://coopmods.com/dev/games.json` does not answer 401 without the password.

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
- **games.json or the manifest unsigned or badly signed** — the same two cases, with
  `coopmods.com sent a file without a valid signature (<reason>)` ([Signatures](#signatures)).

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
| Host | the server `coopmods.com` resolves to; it serves a placeholder at the root |
| Web server | Caddy, in a container named `caddy` |
| `https://coopmods.com/games.json` | games.json — served **without** the cookie |
| `https://coopmods.com/games.json.sig` | its signature ([Signatures](#signatures)) — served **without** the cookie |
| `https://coopmods.com/launcher/FFB%20Co-op.exe` | the launcher's own update — served **without** the cookie |
| `https://mewgenics.coopmods.com/update/manifest.json` and its sibling files | the Mewgenics package, already published by mewgenics-coop, already cookie-free; `manifest.json.sig` beside it from mewgenics-coop#553 |
| `https://coopmods.com/dev/…` | the [dev channel](#dev-channel) (#26): its games.json and `.sig`, `launcher/FFB Co-op - dev.exe`, and `<game id>/manifest.json`, its `.sig` and its files -- behind the shared dev password |

The Caddyfile is a single-file bind mount: **never `sed -i` it** (that replaces the inode and the
container keeps the old file). Edit it in place — open read/write, write, truncate — then
`docker exec caddy caddy reload --config /etc/caddy/Caddyfile`. The cookie gate and its exceptions
work as in mewgenics-coop's `site/README.md`: each cookie-free path is named in the site block.
The publish script and the coopmods.com site block are #7.

## Versions

Release N is "vN" to players and the site, and `N.0.0` in `src/ffb_version.h`, the version resource
and games.json's `launcher.version` (#24; decided by Martin 2026-10-01): v1 is `1.0.0`, then v2, v3, …
The console window's title is `FFB Co-op vN`; the first printed line stays `FFB Co-op N.0.0`.

## Constraints

- Existing players' standalone loader keeps working; there is no forced migration.
- The file is `FFB Co-op.exe`. Its version resource has FileDescription and ProductName
  "FFB Co-op". Its icon is the navy bunnycorn, mewgenics-coop `res/Mewcoop.ico` (commit 0246ab7).
- Windows only, MSVC via CMake, like mewgenics-coop; it builds on Tubal-Cain.

## Testing

**Unit** (tier `code`): the folder finder and the update code as plain functions — an empty folder,
one match, two matches, a malformed games.json, a package file with a bad sha256, a network file
name with a path in it. Every validation rule above is a case. The signature check: valid, missing,
wrong key and a tampered byte, on games.json and on a manifest (#21).

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

Later, quoted exactly:

- **Versions** (2026-10-01 23:27, mewgenics-coop lead pane, #24): "file it as an ffb-coop card for
  the v1 release and prio it" — the signing launcher ships as v1, `1.0.0` on the wire ([Versions](#versions)).
- **Dev channel** (2026-10-02 20:05, mewgenics-coop lead pane, #26): "do a seperate self-updating dev chanel
  that gets the exe from a passworded site on coopmods.com. And it will be me and budda to start with."
  ([Dev channel](#dev-channel)).
- **Dev password** (2026-10-02 21:28, lead pane, #26): "Lets do no login or if we can only use a password", then
  "One shared password" — one password built into the dev exe, no user name, no prompt
  ([The dev password](#the-dev-password)).

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
