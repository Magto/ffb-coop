# coopmods.com -- what FFB Co-op.exe reads

Three things, all served WITHOUT a cookie, because `FFB Co-op.exe` has no password to send:

| URL | File on the server | Written by |
|---|---|---|
| `https://coopmods.com/games.json` | `/opt/downloads/ffb-coop-site/games.json` | `tools/publish.py` |
| `https://coopmods.com/games.json.sig` | `/opt/downloads/ffb-coop-site/games.json.sig` | `tools/publish.py` (#21) |
| `https://coopmods.com/launcher/FFB%20Co-op.exe` | `/opt/downloads/ffb-coop-site/launcher/FFB Co-op.exe` | `tools/publish.py` |

Every other path on `coopmods.com` answers the placeholder text `Mewgenics Coop -- coopmods.com`. What the
launcher does with games.json and its signature is `docs/SPEC.md` (the signature: "Signatures"); the Mewgenics package itself is not here but at
`https://mewgenics.coopmods.com/update/`, published by mewgenics-coop (`tools/publish_site.py`).

## Server

- The server `coopmods.com` and `www` resolve to; `HOST` in `tools/publish.py` is how to reach it.
- Web server: Caddy, container `caddy`, its Caddyfile bind-mounted from the host (`$CADDYFILE` below;
  `/etc/caddy/Caddyfile` in the container). Host `/opt/downloads` is `/downloads` in the container.
- The site block. It **replaces** the `coopmods.com` placeholder block that was there before; it is
  not a second block beside it:

```
# FFB Co-op launcher: games.json and the launcher's own update, served WITHOUT a cookie --
# FFB Co-op.exe has no password to send. Everything else keeps the placeholder.
coopmods.com, www.coopmods.com {
    root * /downloads/ffb-coop-site
    @launcher path /games.json /games.json.sig /launcher/*
    handle @launcher {
        header Cache-Control "no-store"
        file_server
    }
    handle {
        respond "Mewgenics Coop -- coopmods.com" 200
    }
}
```

`Cache-Control: no-store` so a launcher never reads a games.json older than the exe it points at. A file
dropped anywhere else under `ffb-coop-site/` is not served: the matcher names `/games.json`,
`/games.json.sig` and `/launcher/*` only, and `publish.py`'s temporary `.upload-*` files sit outside both.

### Editing the Caddyfile

It is a **single-file bind mount: never `sed -i` it**, and never replace it with an editor that writes a
new file and renames it over the old one. Either replaces the inode, and the container keeps reading the
old file. Instead:

1. `cp "$CADDYFILE" "$CADDYFILE.bak-<date>"`
2. change it in place: open read/write, write, truncate (Python `open(p, "r+")`, `seek(0)`, `write`,
   `truncate()`), and check `stat -c %i` shows the same inode as before;
3. `docker exec caddy caddy validate --config /etc/caddy/Caddyfile`, and only when it passes
4. `docker exec caddy caddy reload --config /etc/caddy/Caddyfile`.

If validate fails, **first** put the backup back, in place, the same way as step 2 (read the `.bak`,
then `r+`, `seek(0)`, `write`, `truncate()`, and check the inode), and only then work out what went
wrong. The live file is already edited at that point: left as it is, Caddy fails to start at the next
container restart and takes Matrix and every other site down with it.

**Before the first signed publish (#21)** the live site block needs `/games.json.sig` added to the
`@launcher` matcher, as above, by the steps here; until then a launcher cannot fetch the signature and
refuses games.json, and `tools/publish.py` refuses to upload.

Content changes (games.json, its signature, the exe) need no reload. Touch no other site block in that file: it also
serves Matrix, mewgenics.coopmods.com, patreon, logs and other sites.

## Publishing

```
python tools/publish.py --release N --dry-run   # check everything, write site/games.json, upload nothing
python tools/publish.py --release N             # the real thing (SHIP_CMD)
```

Release N is v1, v2, ...: `src/ffb_version.h` must say `N.0.0` (rebuild after changing it), and games.json
then carries `"N.0.0"` (docs/SPEC.md, Versions). v1 is `--release 1`.

The games list comes from `site/games.json.in` (edit that to add a game); the `launcher` block is written
from the bytes of `build/Release/FFB Co-op.exe` -- its sha256, its size and the FileVersion in its version
resource. games.json is signed into `site/games.json.sig` with the private key in `COOPMODS_SIGNING_KEY`,
else `~/.config/coopmods/signing.key` (`tools/signing.py`; it needs Python `cryptography`). The script
refuses, and uploads nothing, when:

- `tools/doc_rules.sh` or `tools/scan_rules.sh` fails or is missing;
- the exe has no version resource, or its FileVersion and ProductVersion differ, or `--version` differs;
- the exe's version is not a release version `N.0.0` (N at least 1), or `--release N` is given and the exe
  is not `N.0.0`, or N is not a release number (1, 2, 3, ...);
- the resulting games.json breaks a rule in `docs/SPEC.md` (or `games.json.in` carries its own `launcher`);
- the signing key is missing (a `--dry-run` only warns and writes no `.sig`), unreadable by its owner only,
  not 32 raw bytes, or not one of the keys in `src/trusted_keys.h`;
- `https://coopmods.com/games.json.sig` answers the placeholder: the site block does not route it yet;
- the live games.json has a newer launcher version, or the **same version with different bytes** --
  installed launchers compare versions only, so a rebuilt exe needs a new version to reach anyone.

The exe goes up first, then games.json.sig, then games.json: each to a temporary name, sha256-checked on
the server, then renamed into place, so a launcher reading mid-publish is never sent to bytes that are not
there (between the last two renames a launcher may see the new signature beside the old games.json; it
refuses that once and starts its installed version). Last it fetches all three over https with no cookie
and checks them against what it published, the signature against `src/trusted_keys.h`.

Until the first publish there is no games.json on the server (a 404). That is deliberate: the spec
requires a `launcher` block and at least one game, so there is no valid empty placeholder, and the
launcher treats a 404 like an unreachable server.

Tests: `python -m unittest discover -s tests -p "test_*.py"` (no network, no exe needed). CI runs them
in the `doc-rules` check, and `ctest` runs them as `test_publish`.

## The dev channel (#26)

`FFB Co-op - dev.exe` reads everything under `https://coopmods.com/dev/`, **behind one shared password** (HTTP Basic
with the one user `dev`, Caddy's `basic_auth`). The password is built into the dev exe; there is no prompt and no user
name to type. What the dev exe does with it is `docs/SPEC.md`, "Dev channel".

**Where the password lives.** Only outside every repo: on the lead machine in `~/.config/coopmods/ffb-dev-password`
(WSL side, one line, mode 600). It never goes in a repo, issue, PR or log. The dev exe's build reads it
(`cmake/dev_password.cmake`) from, in order: the environment variable `FFB_DEV_PASSWORD`, the file named by
`FFB_DEV_PASSWORD_FILE`, then `~/.config/coopmods/ffb-dev-password` under `HOME` or `%USERPROFILE%`. With none of
them the build fails ("FFB Co-op - dev.exe needs the dev channel's shared password"); `-DFFB_BUILD_DEV=OFF` at
configure builds `FFB Co-op.exe` alone. From WSL on Tubal-Cain the Windows build gets the file with
`FFB_DEV_PASSWORD_FILE=$HOME/.config/coopmods/ffb-dev-password WSLENV=FFB_DEV_PASSWORD_FILE/p cmake.exe --build …`.
Changing the password means a new hash on the server, then a new dev exe built and published with the new one. A dev
exe already out there carries the old password, so it cannot fetch that update: it starts what is installed with a
one-line warning, and its owner drops the new dev exe in by hand once.

| URL | File on the server | Written by |
|---|---|---|
| `https://coopmods.com/dev/games.json` and `.sig` | `/opt/downloads/ffb-coop-site/dev/games.json` and `.sig` | `tools/publish.py --dev` |
| `https://coopmods.com/dev/launcher/FFB%20Co-op%20-%20dev.exe` | `/opt/downloads/ffb-coop-site/dev/launcher/FFB Co-op - dev.exe` | `tools/publish.py --dev` |
| `https://coopmods.com/dev/<game id>/manifest.json`, `.sig` and its files | `/opt/downloads/ffb-coop-site/dev/<game id>/…` | `tools/publish.py --dev --package <game id> <folder>` |

### What a deploy must do (not done yet)

The lead's deploy session, by the Caddyfile steps under "Editing the Caddyfile" above (backup, edit in place, check
the inode, validate, reload):

1. **Hash the shared password**, at deploy time, from the lead's file, without it ever being on a command line:
   `ssh root@89.167.37.21 docker exec -i caddy caddy hash-password < ~/.config/coopmods/ffb-dev-password`.
   The bcrypt hash it prints goes in the block below and nowhere else; the password itself never goes to the server.
2. **Add the `/dev/*` handle** to the `coopmods.com` block, before the final `handle`:

```
coopmods.com, www.coopmods.com {
    root * /downloads/ffb-coop-site
    @launcher path /games.json /games.json.sig /launcher/*
    handle @launcher {
        header Cache-Control "no-store"
        file_server
    }
    # The dev channel (#26): FFB Co-op - dev.exe and dev packages, one shared password.
    handle /dev/* {
        basic_auth {
            dev <bcrypt hash of the shared dev password>
        }
        header Cache-Control "no-store"
        file_server
    }
    handle {
        respond "Mewgenics Coop -- coopmods.com" 200
    }
}
```

   On a Caddy older than 2.8 the directive is spelled `basicauth`; `caddy validate` says which.
3. **Make sure no other site serves these files.** The same Caddyfile serves Matrix, mewgenics.coopmods.com, patreon,
   logs and others from the same `/downloads` mount; a block whose `root` is `/downloads` or `/downloads/ffb-coop-site`
   would serve the dev files without the password on its own host, and the checks below (and `tools/publish.py --dev`)
   only ask `coopmods.com`. List them with `grep -n 'root' "$CADDYFILE"`: only the `coopmods.com` block may name
   `/downloads/ffb-coop-site`, and none may name `/downloads` itself. Anything else is a stop for Martin before the
   first dev publish.
4. **Make the folder:** `mkdir -p /opt/downloads/ffb-coop-site/dev`.
5. **Check it**, from anywhere:
   - `curl -s -o /dev/null -w '%{http_code}\n' https://coopmods.com/dev/games.json` says `401`;
   - with the password, `curl -s -o /dev/null -w '%{http_code}\n' -K - https://coopmods.com/dev/games.json <<< "user = \"dev:$(cat ~/.config/coopmods/ffb-dev-password)\""`
     says `404` until the first dev publish, `200` after (`-K -` keeps the password off the command line);
   - the public paths are unchanged: `curl -s https://coopmods.com/games.json` still answers games.json without a
     password, and `https://coopmods.com/dev` (no slash) the placeholder.

`tools/publish.py --dev` refuses to upload anything until step 5's first check says 401, so a dev file can never go
up while the gate is missing. There is no per-person revoke: a password that leaked is replaced (above).

### Publishing to the dev channel

```
python tools/publish.py --dev --dry-run                       # check, write site/dev/, upload nothing
python tools/publish.py --dev                                 # the dev exe only
python tools/publish.py --dev --package mewgenics <folder> --package-version 77.0.1 --optional mewcoop_ui.swf
python tools/publish.py --dev --no-exe --package mewgenics <folder> --package-version 77.0.2 --optional mewcoop_ui.swf
```

- **The exe** is `build/Release/FFB Co-op - dev.exe` (built beside `FFB Co-op.exe` by the same `cmake --build`,
  with the shared password available as above).
  Its version must be a dev version `N.0.D` (D at least 1): bump `FFB_DEV_BUILD` and `FFB_DEV_VERSION_STR` in
  `src/ffb_version.h` before each dev publish, or the live comparison refuses it ("bump the version"), exactly as
  for the public exe. `--release` is refused with `--dev`, and the public publish refuses a dev exe.
- **A package** is one flat folder holding the package's files under their real names (for Mewgenics:
  `mewcoop_loader.exe`, `mewcoop.dll`, `mewcoop_ui.swf`). Every file goes in the manifest with its size and sha256,
  required unless named with `--optional`; the manifest is signed, and goes up after its files.
- **The games list** is `site/dev-games.json.in`; Mewgenics reads its dev manifest there. Publishing the exe alone
  is refused while a game's dev manifest is not live yet, so the first dev publish carries a package (or the game is
  pointed at its public manifest in that file).
- Everything goes to `/opt/downloads/ffb-coop-site/dev/` and nowhere else; the temporary upload names are in that
  folder too. The signing key and the gates are the public publish's. It reads the live dev games.json over ssh, so
  publishing needs no password; set `FFB_DEV_PASSWORD` (e.g. `FFB_DEV_PASSWORD=$(cat ~/.config/coopmods/ffb-dev-password)`)
  to have it also read games.json back over https as the dev exe does.
