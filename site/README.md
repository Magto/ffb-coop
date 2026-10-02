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

`FFB Co-op - dev.exe` reads everything under `https://coopmods.com/dev/`, **behind a per-person login** (HTTP Basic,
Caddy's `basic_auth`). What the dev exe does with it, and why a per-person login, is `docs/SPEC.md`, "Dev channel".

| URL | File on the server | Written by |
|---|---|---|
| `https://coopmods.com/dev/games.json` and `.sig` | `/opt/downloads/ffb-coop-site/dev/games.json` and `.sig` | `tools/publish.py --dev` |
| `https://coopmods.com/dev/launcher/FFB%20Co-op%20-%20dev.exe` | `/opt/downloads/ffb-coop-site/dev/launcher/FFB Co-op - dev.exe` | `tools/publish.py --dev` |
| `https://coopmods.com/dev/<game id>/manifest.json`, `.sig` and its files | `/opt/downloads/ffb-coop-site/dev/<game id>/…` | `tools/publish.py --dev --package <game id> <folder>` |

### What a deploy must do (not done yet)

The lead's deploy session, by the Caddyfile steps under "Editing the Caddyfile" above (backup, edit in place, check
the inode, validate, reload):

1. **Make the two logins.** For each person, a password of at least 16 random characters, and its bcrypt hash:
   `docker exec caddy caddy hash-password --plaintext '<password>'`. The password goes to that person only, by a
   private channel (Martin to budda directly); the hash goes in the block below. Neither goes in any repo, issue or
   PR.
2. **Add the `/dev/*` handle** to the `coopmods.com` block, before the final `handle`:

```
coopmods.com, www.coopmods.com {
    root * /downloads/ffb-coop-site
    @launcher path /games.json /games.json.sig /launcher/*
    handle @launcher {
        header Cache-Control "no-store"
        file_server
    }
    # The dev channel (#26): FFB Co-op - dev.exe and dev packages, one login per person.
    handle /dev/* {
        basic_auth {
            martin <bcrypt hash of Martin's password>
            budda  <bcrypt hash of budda's password>
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
3. **Make the folder:** `mkdir -p /opt/downloads/ffb-coop-site/dev`.
4. **Check it**, from anywhere:
   - `curl -s -o /dev/null -w '%{http_code}\n' https://coopmods.com/dev/games.json` says `401`;
   - with a login, `curl -s -o /dev/null -w '%{http_code}\n' -u martin https://coopmods.com/dev/games.json` says `404`
     until the first dev publish, `200` after;
   - the public paths are unchanged: `curl -s https://coopmods.com/games.json` still answers games.json without a
     login, and `https://coopmods.com/dev` (no slash) the placeholder.

`tools/publish.py --dev` refuses to upload anything until step 4's first check says 401, so a dev file can never go
up while the gate is missing. Revoking a person later is deleting their line and reloading.

### Publishing to the dev channel

```
python tools/publish.py --dev --dry-run                       # check, write site/dev/, upload nothing
python tools/publish.py --dev                                 # the dev exe only
python tools/publish.py --dev --package mewgenics <folder> --package-version 77.0.1 --optional mewcoop_ui.swf
python tools/publish.py --dev --no-exe --package mewgenics <folder> --package-version 77.0.2 --optional mewcoop_ui.swf
```

- **The exe** is `build/Release/FFB Co-op - dev.exe` (built beside `FFB Co-op.exe` by the same `cmake --build`).
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
  publishing needs no login; set `FFB_DEV_LOGIN=user:password` to have it also read games.json back over https.
