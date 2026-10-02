"""Publish FFB Co-op.exe and games.json to coopmods.com (SHIP_CMD, #7).

    python tools/publish.py [--release N] [--exe "build/Release/FFB Co-op.exe"] [--dry-run]

Release N is version "N.0.0" (docs/SPEC.md, "Versions"): v1 is 1.0.0, v2 is 2.0.0. Players and the site say
"vN"; games.json and the exe's version resource carry "N.0.0".

What it does, in order, and what makes it refuse:

1. Runs tools/doc_rules.sh and tools/scan_rules.sh; refuses on a non-zero exit, or when either is missing
   (a --dry-run only warns about a missing one).
2. Reads the exe: its sha256, its size and its version from the version resource (VS_FIXEDFILEINFO,
   FileVersion MAJOR.MINOR.PATCH). Refuses when there is no version resource, when FileVersion and
   ProductVersion disagree, when it is not a release version N.0.0, when --release N is given and the exe is
   not N.0.0 (set src/ffb_version.h and rebuild), or when --version is given and differs from the exe's.
3. Builds games.json: the `launcher` block from those bytes, the games list from site/games.json.in, and
   checks the result against every docs/SPEC.md rule (a file the launcher would reject is never written).
   Signs it (#21, tools/signing.py): games.json.sig beside it, with the private key from COOPMODS_SIGNING_KEY or
   ~/.config/coopmods/signing.key. Refuses without that key, or with one whose public key is not in
   src/trusted_keys.h (the launcher would refuse what it signs). A --dry-run without the key only warns.
4. Compares with the games.json that is live now. Refuses a lower launcher version (installed launchers
   would never take it) and the same version with different bytes (installed launchers compare versions
   only, so they would never fetch the new bytes -- bump the version).
5. Checks that coopmods.com routes /games.json.sig to the files (not the placeholder), so a launcher can
   fetch the signature. Uploads the exe under a temporary name, checks its sha256 on the server, moves it
   over launcher/FFB Co-op.exe; then games.json.sig and games.json the same way, games.json LAST, so it never
   advertises bytes that are not up yet. Then fetches all three over https WITHOUT a cookie and checks them,
   the signature against the trusted keys.

--dry-run does 1-4 (the live comparison only if the server answers) and writes site/games.json (and
site/games.json.sig when the key is there), and uploads nothing. The server layout and the Caddy block are in site/README.md.

The dev channel (#26, docs/SPEC.md "Dev channel") -- FFB Co-op - dev.exe and dev packages, for Martin and budda:

    python tools/publish.py --dev [--exe "build/Release/FFB Co-op - dev.exe" | --no-exe]
                                  [--package GAME_ID DIR --package-version V [--optional NAME ...]] [--dry-run]

Everything it writes on the server is under /opt/downloads/ffb-coop-site/dev/, served at coopmods.com/dev/ behind
the dev login; it never touches a public file (dev_put refuses any other path). The same gates, the same signing key,
the same version rules against the live dev games.json (read over ssh: publishing needs no login). The exe must be a
dev version MAJOR.0.D with D >= 1, which no release exe ever is; --release is refused with --dev. The games list is
site/dev-games.json.in. --package writes a manifest for every file in DIR (all required except --optional ones),
signs it and uploads it to coopmods.com/dev/<GAME_ID>/; that game's manifest in dev-games.json.in must be that URL.
Before uploading it checks that coopmods.com/dev/games.json answers 401 without a login, so nothing goes up while
the gate is not deployed; afterwards, that every published URL still does.
"""
import argparse, base64, hashlib, json, os, re, struct, subprocess, sys, tempfile, urllib.error, urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import signing  # noqa: E402  -- tools/signing.py, #21

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOST = "root@89.167.37.21"
REMOTE = "/opt/downloads/ffb-coop-site"      # /downloads/ffb-coop-site in the caddy container
URL = "https://coopmods.com"
EXE_NAME = "FFB Co-op.exe"
LAUNCHER_URL = f"{URL}/launcher/FFB%20Co-op.exe"
GAMES_URL = f"{URL}/games.json"
SIG_URL = GAMES_URL + signing.SIG_SUFFIX
PLACEHOLDER = b"Mewgenics Coop -- coopmods.com"   # what the site block answers for a path it does not route
DEFAULT_EXE = os.path.join(REPO, "build", "Release", EXE_NAME)
GAMES_IN = os.path.join(REPO, "site", "games.json.in")
GATES = ("tools/doc_rules.sh", "tools/scan_rules.sh")   # DOC_RULES_CMD, SCAN_RULES_CMD

# The dev channel (#26): one folder on the server, one URL prefix behind the dev login.
DEV_REMOTE = REMOTE + "/dev"
DEV_URL = URL + "/dev"
DEV_EXE_NAME = "FFB Co-op - dev.exe"
DEV_LAUNCHER_URL = f"{DEV_URL}/launcher/FFB%20Co-op%20-%20dev.exe"
DEV_GAMES_URL = f"{DEV_URL}/games.json"
DEV_SIG_URL = DEV_GAMES_URL + signing.SIG_SUFFIX
DEV_DEFAULT_EXE = os.path.join(REPO, "build", "Release", DEV_EXE_NAME)
DEV_GAMES_IN = os.path.join(REPO, "site", "dev-games.json.in")
DEV_OUT_DIR = os.path.join(REPO, "site", "dev")
DEV_LOGIN_ENV = "FFB_DEV_LOGIN"   # "user:password": optional, lets the post-publish check read the files back


class Refused(Exception):
    """A reason not to publish. main() prints it and exits 1."""


# ---- the exe ---------------------------------------------------------------------------------------

VS_FIXEDFILEINFO_SIG = struct.pack("<I", 0xFEEF04BD)


def exe_version(data):
    """-> "MAJOR.MINOR.PATCH" from the VS_FIXEDFILEINFO in a PE's version resource.

    Found by its signature rather than by walking the resource tree, which is enough for a file we built
    ourselves. The fourth (build) part is not part of the spec's version and is ignored."""
    i = data.find(VS_FIXEDFILEINFO_SIG)
    if i < 0 or i + 24 > len(data):
        raise Refused("the exe has no version resource (VS_FIXEDFILEINFO not found)")
    _sig, _struc, fms, fls, pms, pls = struct.unpack_from("<6I", data, i)
    fv = (fms >> 16, fms & 0xFFFF, fls >> 16)
    pv = (pms >> 16, pms & 0xFFFF, pls >> 16)
    if fv != pv:
        raise Refused("the exe's FileVersion %d.%d.%d and ProductVersion %d.%d.%d disagree" % (fv + pv))
    return "%d.%d.%d" % fv


def launcher_block(data, url=LAUNCHER_URL):
    return {"version": exe_version(data), "url": url,
            "sha256": hashlib.sha256(data).hexdigest(), "size": len(data)}


# ---- games.json: the rules of docs/SPEC.md, "Validation, field by field" -----------------------------

VERSION_RE = re.compile(r"(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)")
HEX64_RE = re.compile(r"[0-9a-fA-F]{64}")
ID_RE = re.compile(r"[a-z0-9-]{1,32}")
RESERVED = ({"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "CLOCK$"}
            | {f"{d}{i}" for d in ("COM", "LPT") for i in "123456789\u00b9\u00b2\u00b3"})   # ¹ ² ³


def version_key(v):
    return tuple(int(p) for p in v.split("."))


RELEASE_RE = re.compile(r"[1-9]\d*")


def release_version(n):
    """Release number N ("1", "2", ...) -> "N.0.0", the version games.json carries for vN (#24)."""
    if not isinstance(n, str) or not RELEASE_RE.fullmatch(n):
        raise Refused(f"release {n!r} is not a release number 1, 2, 3, ...")
    return f"{n}.0.0"


def is_release_version(v):
    """True for "N.0.0" with N >= 1: every published launcher is vN."""
    m = VERSION_RE.fullmatch(v)
    return bool(m) and m.group(1) != "0" and m.group(2) == "0" and m.group(3) == "0"


def is_dev_version(v):
    """True for "N.0.D" with N >= 1 and D >= 1: a dev build (src/ffb_version.h, FFB_DEV_BUILD). Never a release."""
    m = VERSION_RE.fullmatch(v)
    return bool(m) and m.group(1) != "0" and m.group(2) == "0" and m.group(3) != "0"


def plain_name_problem(name):
    """None if `name` is a plain file name (SPEC "Network file-name rule"), else why not."""
    if not isinstance(name, str) or not name:
        return "empty or not a string"
    if len(name) > 255:
        return "longer than 255 characters"
    if any(c in name for c in "/\\:"):
        return "contains / \\ or :"
    if any(ord(c) < 0x20 or ord(c) == 0x7F for c in name):
        return "contains a control character"
    if any(c in name for c in '<>"|?*'):
        return "contains a character Windows does not allow (< > \" | ? *)"
    if ".." in name or name == ".":
        return "is . or contains .."
    if name != name.strip(" ") or name.endswith("."):
        return "leading/trailing space or trailing ."
    # Windows drops trailing spaces from the part before the first dot: "nul .txt" is the device too.
    if name.split(".")[0].rstrip(" ").upper() in RESERVED:
        return "a Windows reserved device name"
    return None


def _is_int(v):
    return isinstance(v, int) and not isinstance(v, bool)


def _https(v):
    return isinstance(v, str) and v.startswith("https://") and len(v) > len("https://")


def validate_games_json(doc):
    """-> list of problems; empty means the launcher would accept `doc`."""
    p = []
    if not isinstance(doc, dict):
        return ["top level is not an object"]
    if not _is_int(doc.get("schema")) or doc.get("schema") != 1:
        p.append("schema must be the integer 1")
    la = doc.get("launcher")
    if not isinstance(la, dict):
        p.append("launcher missing or not an object")
    else:
        if not isinstance(la.get("version"), str) or not VERSION_RE.fullmatch(la["version"]):
            p.append("launcher.version is not MAJOR.MINOR.PATCH")
        if not _https(la.get("url")):
            p.append("launcher.url is not an absolute https:// URL")
        if not isinstance(la.get("sha256"), str) or not HEX64_RE.fullmatch(la["sha256"]):
            p.append("launcher.sha256 is not 64 hex digits")
        if not _is_int(la.get("size")) or la["size"] < 1:
            p.append("launcher.size is not an integer >= 1")
    games = doc.get("games")
    if not isinstance(games, list) or not games:
        p.append("games missing or empty")
        return p
    ids, exes = set(), set()
    for n, g in enumerate(games):
        w = f"games[{n}]"
        if not isinstance(g, dict):
            p.append(f"{w} is not an object"); continue
        gid = g.get("id")
        if not isinstance(gid, str) or not ID_RE.fullmatch(gid):
            p.append(f"{w}.id must be 1-32 of a-z 0-9 -")
        elif gid in ids:
            p.append(f"{w}.id {gid!r} is not unique")
        else:
            ids.add(gid)
        if not isinstance(g.get("name"), str) or not g["name"]:
            p.append(f"{w}.name is empty")
        for key in ("exe", "launcher"):
            v = g.get(key)
            why = plain_name_problem(v)
            if why:
                p.append(f"{w}.{key} {v!r}: {why}")
            elif not v.lower().endswith(".exe"):
                p.append(f"{w}.{key} {v!r} does not end in .exe")
        exe = g.get("exe")
        if isinstance(exe, str):
            if exe.lower() in exes:
                p.append(f"{w}.exe {exe!r} is not unique")
            exes.add(exe.lower())
        if not _is_int(g.get("steam_appid")) or g["steam_appid"] < 0:
            p.append(f"{w}.steam_appid is not an integer >= 0")
        if not _https(g.get("manifest")):
            p.append(f"{w}.manifest is not an absolute https:// URL")
    return p


def build_games_json(games_in, exe_bytes=None, launcher=None):
    """-> the games.json document: the launcher block from `exe_bytes` (or `launcher` as given), the games from
    `games_in` (the parsed site/games.json.in). Refuses rather than return a file the launcher would reject."""
    if not isinstance(games_in, dict) or games_in.get("schema") != 1:
        raise Refused("site/games.json.in must be an object with \"schema\": 1")
    if "launcher" in games_in:
        raise Refused("site/games.json.in must not carry a launcher block -- it is written from the exe")
    doc = {"schema": 1, "launcher": launcher if launcher is not None else launcher_block(exe_bytes),
           "games": games_in.get("games")}
    problems = validate_games_json(doc)
    if problems:
        raise Refused("games.json would be invalid:\n  " + "\n  ".join(problems))
    return doc


def check_against_live(new, live):
    """Refuse what installed launchers would never pick up. `live` is the parsed live games.json or None."""
    if not isinstance(live, dict) or validate_games_json(live):
        return   # nothing (valid) there yet: any version is an update
    old, cur = live["launcher"], new["launcher"]
    if version_key(cur["version"]) < version_key(old["version"]):
        raise Refused(f"launcher {cur['version']} is older than the live {old['version']}")
    if cur["version"] == old["version"] and (cur["sha256"].lower() != old["sha256"].lower()
                                             or cur["size"] != old["size"]):
        raise Refused(f"launcher {cur['version']} is already live with different bytes -- installed "
                      "launchers compare versions only and would never fetch these; bump the version")


def under_dev(url):
    return isinstance(url, str) and url.startswith(DEV_URL + "/")


def check_no_dev_urls(doc):
    """The public games.json never points at the dev channel (#26): a public launcher has no login for it."""
    urls = [doc["launcher"]["url"]] + [g["manifest"] for g in doc["games"]]
    bad = [u for u in urls if under_dev(u)]
    if bad:
        raise Refused("the public games.json would point at the dev channel: " + ", ".join(bad))


def dumps(doc):
    return json.dumps(doc, indent=2) + "\n"


# ---- the dev channel's package manifest (#26) --------------------------------------------------------

def dev_manifest_url(game_id):
    return f"{DEV_URL}/{game_id}/manifest.json"


def build_package_manifest(folder, version, optional=()):
    """-> the manifest of every file in `folder` (docs/SPEC.md, "The game manifest"): its name, size and sha256,
    required unless named in `optional`. Refuses a folder the launcher could not install from."""
    if not isinstance(version, str) or not (VERSION_RE.fullmatch(version) or re.fullmatch(r"[0-9]+", version)):
        raise Refused(f"--package-version {version!r} is not N or X.Y.Z")
    if not os.path.isdir(folder):
        raise Refused(f"--package folder {folder} does not exist")
    names = sorted(os.listdir(folder))
    files, seen = [], set()
    for name in names:
        path = os.path.join(folder, name)
        if not os.path.isfile(path):
            raise Refused(f"{path} is not a file -- a package is one flat folder")
        if name == "manifest.json" or name.endswith(signing.SIG_SUFFIX):
            raise Refused(f"{path}: the manifest and its .sig are written by this script, not taken from the folder")
        why = plain_name_problem(name)
        if why:
            raise Refused(f"{path}: {why}")
        if name.lower() in seen:
            raise Refused(f"{path}: two files with this name in different case")
        seen.add(name.lower())
        with open(path, "rb") as f:
            data = f.read()
        if not data:
            raise Refused(f"{path} is empty")
        files.append({"name": name, "size": len(data), "sha256": hashlib.sha256(data).hexdigest(),
                      "required": name not in optional})
    if not files:
        raise Refused(f"--package folder {folder} is empty")
    missing = [o for o in optional if o not in [f["name"] for f in files]]
    if missing:
        raise Refused(f"--optional names a file that is not in {folder}: {', '.join(missing)}")
    return {"version": version, "files": files}


# ---- the network -----------------------------------------------------------------------------------

def run(cmd, check=True):
    print(">", " ".join(cmd), flush=True)
    r = subprocess.run(cmd, text=True, capture_output=True)
    if check and r.returncode != 0:
        print(r.stdout[-2000:], r.stderr[-2000:])
        raise Refused(f"{cmd[0]} failed (exit {r.returncode})")
    return r


def fetch(url, timeout=20):
    """-> bytes, fetched with no cookie; None on a 404; raises on anything else."""
    req = urllib.request.Request(url, headers={"User-Agent": "ffb-coop-publish"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.read()
    except urllib.error.HTTPError as e:
        if e.code == 404:
            return None
        raise


def fetch_live_games():
    data = fetch(f"{URL}/games.json")
    if data is None:
        return None
    try:
        return json.loads(data.decode("utf-8"))
    except ValueError:
        return None


def find_bash():
    """Git Bash on Windows: a bare `bash` there can be WSL's System32\\bash.exe, which runs in another tree."""
    if os.name == "nt":
        for p in (r"C:\Program Files\Git\bin\bash.exe", r"C:\Program Files (x86)\Git\bin\bash.exe"):
            if os.path.exists(p):
                return p
    return "bash"


def run_gates(dry_run):
    bash = find_bash()
    for gate in GATES:
        path = os.path.join(REPO, gate)
        if not os.path.exists(path):
            if dry_run:
                print(f"warning: {gate} is missing (it lands with #1); a real publish refuses")
                continue
            raise Refused(f"{gate} is missing")
        r = subprocess.run([bash, gate], cwd=REPO, text=True, capture_output=True)
        if r.returncode != 0:
            print(r.stdout[-3000:], r.stderr[-2000:])
            raise Refused(f"{gate} exited {r.returncode}")
        print(f"{gate}: ok")


def remote_put(local, remote_name, sha256, remote=REMOTE):
    """scp to a temporary name, check the sha256 there, then move into place (a rename, so a reader never
    sees half a file). `remote_name` is relative to `remote` and may contain a space."""
    tmp = f"{remote}/.upload-{sha256[:16]}"
    run(["scp", "-q", local, f"{HOST}:{tmp}"])
    r = run(["ssh", "-o", "ConnectTimeout=15", HOST, f"sha256sum {tmp}"])
    got = r.stdout.split()[0] if r.stdout.split() else ""
    if got.lower() != sha256.lower():
        run(["ssh", HOST, f"rm -f {tmp}"], check=False)
        raise Refused(f"{remote_name} arrived with sha256 {got or '?'}, expected {sha256}")
    run(["ssh", HOST, f"mv -f {tmp} '{remote}/{remote_name}'"])


def dev_put(local, remote_name, sha256):
    """remote_put into the dev folder, and nowhere else (#26): `remote_name` is one or two plain file names
    joined by "/", so neither the upload nor its temporary file can land outside /dev/."""
    parts = remote_name.split("/") if isinstance(remote_name, str) else []
    if not 1 <= len(parts) <= 2 or any(plain_name_problem(p) for p in parts):
        raise Refused(f"refusing to upload {remote_name!r}: not a plain path inside {DEV_REMOTE}")
    remote_put(local, remote_name, sha256, remote=DEV_REMOTE)


def verify_live(doc, keys):
    """Fetch all three over https with no cookie and check them against what was published; the signature
    must sign the live games.json bytes under one of `keys` (the launcher's)."""
    raw = fetch(GAMES_URL)
    try:
        live = None if raw is None else json.loads(raw.decode("utf-8"))
    except ValueError:   # not JSON at all, e.g. the placeholder page
        live = None
    if live != doc:
        raise Refused(f"{URL}/games.json does not answer the published file without a cookie")
    sig = fetch(SIG_URL)
    if sig is None or not signing.verify_bytes(raw, sig, keys):
        raise Refused(f"{SIG_URL} does not answer a valid signature of the live games.json without a cookie")
    exe = fetch(LAUNCHER_URL, timeout=120)
    la = doc["launcher"]
    if exe is None or len(exe) != la["size"] or hashlib.sha256(exe).hexdigest() != la["sha256"]:
        raise Refused(f"{LAUNCHER_URL} does not answer the published exe without a cookie")
    print(f"verified without a cookie: games.json, its signature and {EXE_NAME} {la['version']} "
          f"({la['size']} bytes)")


def check_sig_route():
    """Refuse before uploading anything when coopmods.com does not serve /games.json.sig from the files: the
    site block answers its placeholder for a path it does not route (site/README.md), and a launcher that
    cannot fetch the signature refuses games.json."""
    try:
        data = fetch(SIG_URL)
    except Exception as e:   # noqa: BLE001 -- any network failure
        raise Refused(f"cannot read {SIG_URL}: {e}")
    if data is not None and data.strip() == PLACEHOLDER:
        raise Refused(f"coopmods.com does not route {SIG_URL} to the files yet -- add /games.json.sig to the "
                      "site block's matcher first (site/README.md)")


def signing_key(dry_run):
    """The private key, checked against src/trusted_keys.h. None on a --dry-run without one."""
    try:
        return signing.load_trusted_key(REPO)
    except signing.SigningError as e:
        if dry_run and not os.path.isfile(signing.key_path()):
            print(f"warning: {e}; games.json is not signed, and a real publish refuses")
            return None
        raise Refused(str(e))


def http_status(url, login=None, timeout=20):
    """-> (HTTP status, body) for a GET of `url`, with HTTP Basic `login` ("user:password") when given; raises on
    a transport failure."""
    headers = {"User-Agent": "ffb-coop-publish"}
    if login:
        headers["Authorization"] = "Basic " + base64.b64encode(login.encode("utf-8")).decode("ascii")
    try:
        with urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=timeout) as r:
            return r.status, r.read()
    except urllib.error.HTTPError as e:
        return e.code, b""


def check_dev_gate():
    """Refuse before uploading anything unless coopmods.com/dev/ answers 401 without a login: a dev file put up
    while the gate is not deployed would be public (site/README.md, "The dev channel")."""
    try:
        code, body = http_status(DEV_GAMES_URL)
    except Exception as e:   # noqa: BLE001 -- any network failure
        raise Refused(f"cannot read {DEV_GAMES_URL}: {e}")
    if code != 401:
        raise Refused(f"{DEV_GAMES_URL} answered HTTP {code} without a login, not 401 -- the dev gate is not up; "
                      "deploy the site block in site/README.md first")


def ssh_answer(cmd, what):
    """Runs `cmd` on the server. -> True on exit 0, False on exit 1 (the file is not there); raises Refused on
    anything else -- ssh itself fails with 255 -- so a dropped connection never reads as a missing file."""
    r = run(["ssh", "-o", "ConnectTimeout=15", HOST, cmd], check=False)
    if r.returncode in (0, 1):
        return r.returncode == 0, r
    raise Refused(f"could not {what} on the server (ssh exit {r.returncode})")


def ssh_read(path):
    """-> the text of a file on the server, or None when it is not there."""
    ok, r = ssh_answer(f"cat '{path}'", f"read {path}")
    return r.stdout if ok else None


def remote_exists(path):
    return ssh_answer(f"test -f '{path}'", f"look for {path}")[0]


def fetch_live_dev_games():
    """The live dev games.json, read over ssh (publishing needs no dev login). None when there is none."""
    text = ssh_read(f"{DEV_REMOTE}/games.json")
    if text is None:
        return None
    try:
        return json.loads(text)
    except ValueError:
        return None


def verify_dev_live(urls, games_text):
    """After a dev publish: every published URL still answers 401 without a login (nothing went public), and,
    with FFB_DEV_LOGIN set, games.json answers the published bytes with it."""
    for url in urls:
        code, _ = http_status(url)
        if code != 401:
            raise Refused(f"{url} answered HTTP {code} without a login -- the dev gate is not covering it")
    login = os.environ.get(DEV_LOGIN_ENV)
    if login:
        code, body = http_status(DEV_GAMES_URL, login=login)
        if code != 200 or body != games_text.encode("utf-8"):
            raise Refused(f"{DEV_GAMES_URL} with {DEV_LOGIN_ENV} does not answer the published games.json "
                          f"(HTTP {code})")
        print(f"verified with the dev login: {DEV_GAMES_URL}")
    else:
        print(f"{DEV_LOGIN_ENV} not set: the dev files were checked by sha256 on the server, not read back over https")


def write_signed(path, text, key, keys):
    """Writes `text` to `path` and, with a key, `path`.sig beside it, checked against `keys`. Never leaves a .sig
    of older bytes behind."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    sig = path + signing.SIG_SUFFIX
    if os.path.exists(sig):
        os.remove(sig)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    if key is not None:
        signing.sign_file(path, key)
        with open(path, "rb") as f, open(sig, "rb") as g:
            if not signing.verify_bytes(f.read(), g.read(), keys):
                raise Refused(f"{sig} does not verify against {signing.TRUSTED_KEYS_H}")
        print(f"signed {sig} with {signing.public_hex(key)}")
    return sig


def sha256_of(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def publish_dev(a):
    """The dev channel's publish (#26). See the module docstring."""
    if a.release is not None:
        raise Refused("--release is the public channel's; a dev build is published with --dev alone")
    if a.no_exe and a.exe_given:
        raise Refused("--exe and --no-exe together")
    packages = a.package or []
    if packages and not a.package_version:
        raise Refused("--package needs --package-version")
    if a.optional and not packages:
        raise Refused("--optional without --package")
    if a.no_exe and not packages:
        raise Refused("--no-exe and no --package: nothing to publish")
    run_gates(a.dry_run)
    with open(a.games_in, encoding="utf-8") as f:
        games_in = json.load(f)

    try:
        live = fetch_live_dev_games()
    except Exception as e:   # noqa: BLE001 -- any ssh failure, Refused included
        if not a.dry_run:
            raise Refused(f"cannot read the live dev games.json on the server: {e}")
        print(f"warning: live dev games.json not read ({e}); skipped the live comparison")
        live = None

    exe = None
    if a.no_exe:
        if not isinstance(live, dict) or validate_games_json(live):
            raise Refused("--no-exe keeps the live dev launcher, and there is no valid live dev games.json")
        launcher = live["launcher"]
    else:
        if not os.path.exists(a.exe):
            raise Refused(f"missing: {a.exe} (build it first: cmake --build build --config Release)")
        with open(a.exe, "rb") as f:
            exe = f.read()
        launcher = launcher_block(exe, DEV_LAUNCHER_URL)
        if not is_dev_version(launcher["version"]):
            raise Refused(f"the exe says {launcher['version']}, not a dev version MAJOR.0.D with D >= 1 -- build "
                          "FFB Co-op - dev.exe, and bump FFB_DEV_BUILD in src/ffb_version.h")
        if a.version and a.version != launcher["version"]:
            raise Refused(f"--version {a.version} but the exe says {launcher['version']}")
    doc = build_games_json(games_in, launcher=launcher)
    if not under_dev(doc["launcher"]["url"]):
        raise Refused(f"the dev launcher URL {doc['launcher']['url']} is not under {DEV_URL}/")
    check_against_live(doc, live)

    by_id = {g["id"]: g for g in doc["games"]}
    manifests = []   # (game id, folder, manifest doc)
    for game_id, folder in packages:
        game = by_id.get(game_id)
        if game is None:
            raise Refused(f"--package {game_id}: no such game in {os.path.relpath(a.games_in, REPO)}")
        if game["manifest"] != dev_manifest_url(game_id):
            raise Refused(f"--package {game_id}: its manifest in {os.path.relpath(a.games_in, REPO)} is "
                          f"{game['manifest']}, not {dev_manifest_url(game_id)}")
        m = build_package_manifest(folder, a.package_version, a.optional or ())
        # The launcher refuses a manifest that does not list the game's launcher as a required file
        # (docs/SPEC.md, games[].launcher), so such a package would never install.
        if not any(f["name"] == game["launcher"] and f["required"] for f in m["files"]):
            raise Refused(f"--package {game_id}: {folder} must hold {game['launcher']}, the game's launcher, and it "
                          "cannot be --optional")
        manifests.append((game_id, folder, m))
    published = {m[0] for m in manifests}
    for g in doc["games"]:
        if under_dev(g["manifest"]) and g["id"] not in published:
            if g["manifest"] != dev_manifest_url(g["id"]):
                raise Refused(f"{g['id']}: dev manifest {g['manifest']} is not {dev_manifest_url(g['id'])}")
            if a.dry_run:
                print(f"note: {g['id']} reads {g['manifest']}; a real publish refuses unless it is live")
            elif not remote_exists(f"{DEV_REMOTE}/{g['id']}/manifest.json"):
                raise Refused(f"{g['id']} reads {g['manifest']}, which is not published -- add "
                              f"--package {g['id']} <folder>, or point it at the public manifest")
    key = signing_key(a.dry_run)
    keys = signing.trusted_keys(REPO)

    out_dir = a.out or DEV_OUT_DIR
    games_text = dumps(doc)
    games_out = os.path.join(out_dir, "games.json")
    games_sig = write_signed(games_out, games_text, key, keys)
    written = []
    for game_id, folder, m in manifests:
        path = os.path.join(out_dir, game_id, "manifest.json")
        written.append((game_id, folder, m, path, write_signed(path, dumps(m), key, keys)))
        print(f"wrote {path}: {game_id} {m['version']}, {len(m['files'])} file(s)")
    print(f"wrote {games_out}: dev launcher {doc['launcher']['version']}, sha256 {doc['launcher']['sha256']}, "
          f"{len(doc['games'])} game(s)")
    if a.dry_run:
        print("dry run: nothing uploaded")
        return

    check_dev_gate()
    dirs = " ".join(f"'{DEV_REMOTE}/{d}'" for d in ["launcher"] + [w[0] for w in written])
    run(["ssh", "-o", "ConnectTimeout=15", HOST, f"mkdir -p {dirs}"])
    urls = [DEV_GAMES_URL, DEV_SIG_URL]
    if exe is not None:
        dev_put(a.exe, f"launcher/{DEV_EXE_NAME}", doc["launcher"]["sha256"])
        urls.append(DEV_LAUNCHER_URL)
    # Each package's files, then its .sig, then its manifest; games.json.sig and games.json LAST -- the same
    # order as the public publish, so no reader is sent to bytes that are not up yet.
    for game_id, folder, m, path, sig in written:
        for f in m["files"]:
            dev_put(os.path.join(folder, f["name"]), f"{game_id}/{f['name']}", f["sha256"])
        dev_put(sig, f"{game_id}/manifest.json{signing.SIG_SUFFIX}", sha256_of(sig))
        dev_put(path, f"{game_id}/manifest.json", sha256_of(path))
        urls.append(dev_manifest_url(game_id))
    dev_put(games_sig, f"games.json{signing.SIG_SUFFIX}", sha256_of(games_sig))
    dev_put(games_out, "games.json", sha256_of(games_out))
    verify_dev_live(urls, games_text)
    print("PUBLISHED (dev)", DEV_GAMES_URL)


# ---- main ------------------------------------------------------------------------------------------

def publish(a):
    run_gates(a.dry_run)
    if not os.path.exists(a.exe):
        raise Refused(f"missing: {a.exe} (build it first: cmake --build build --config Release)")
    with open(a.exe, "rb") as f:
        exe_bytes = f.read()
    with open(a.games_in, encoding="utf-8") as f:
        games_in = json.load(f)
    doc = build_games_json(games_in, exe_bytes)
    check_no_dev_urls(doc)
    ver = doc["launcher"]["version"]
    if a.release is not None and release_version(a.release) != ver:
        raise Refused(f"--release {a.release} is {release_version(a.release)} but the exe says {ver} -- "
                      f"set src/ffb_version.h to {release_version(a.release)} and rebuild")
    if not is_release_version(ver):
        raise Refused(f"the exe says {ver}, not a release version N.0.0 (v1 is 1.0.0) -- see docs/SPEC.md, Versions")
    if a.version and a.version != ver:
        raise Refused(f"--version {a.version} but the exe says {ver}")
    try:
        live = fetch_live_games()
    except Exception as e:   # noqa: BLE001 -- any network failure
        if not a.dry_run:
            raise Refused(f"cannot read the live {URL}/games.json: {e}")
        print(f"warning: live games.json not read ({e}); skipped the live comparison")
        live = None
    check_against_live(doc, live)
    key = signing_key(a.dry_run)

    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    text = dumps(doc)
    sig_out = a.out + signing.SIG_SUFFIX
    if os.path.exists(sig_out):
        os.remove(sig_out)   # never leave a .sig of an older games.json beside the new one
    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print(f"wrote {a.out}: launcher {ver}, sha256 {doc['launcher']['sha256']}, "
          f"{doc['launcher']['size']} bytes, {len(doc['games'])} game(s)")
    keys = signing.trusted_keys(REPO)
    if key is not None:
        signing.sign_file(a.out, key)
        with open(a.out, "rb") as f, open(sig_out, "rb") as g:
            if not signing.verify_bytes(f.read(), g.read(), keys):
                raise Refused(f"{sig_out} does not verify against {signing.TRUSTED_KEYS_H}")
        print(f"signed {sig_out} with {signing.public_hex(key)}")
    if a.dry_run:
        print("dry run: nothing uploaded")
        return

    check_sig_route()
    run(["ssh", "-o", "ConnectTimeout=15", HOST, f"mkdir -p {REMOTE}/launcher"])
    remote_put(a.exe, f"launcher/{EXE_NAME}", doc["launcher"]["sha256"])
    # The signature, then games.json LAST: a launcher reading mid-publish must never be sent to bytes that
    # are not up yet. Between the two moves a launcher sees the new .sig beside the old games.json, refuses
    # it and starts its installed version once -- the safe way round.
    with open(sig_out, "rb") as f:
        remote_put(sig_out, "games.json.sig", hashlib.sha256(f.read()).hexdigest())
    remote_put(a.out, "games.json", hashlib.sha256(text.encode("utf-8")).hexdigest())
    verify_live(doc, keys)
    print("PUBLISHED", f"{URL}/games.json")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--dev", action="store_true", help="publish to the dev channel, coopmods.com/dev/ (#26)")
    ap.add_argument("--exe", help="the built launcher (default: build/Release/FFB Co-op.exe, or FFB Co-op - dev.exe "
                                  "with --dev)")
    ap.add_argument("--no-exe", action="store_true", help="--dev: keep the live dev launcher, publish a package only")
    ap.add_argument("--package", nargs=2, action="append", metavar=("GAME_ID", "DIR"),
                    help="--dev: publish every file in DIR as GAME_ID's dev package")
    ap.add_argument("--package-version", help="--dev: the version the package's manifest shows (N or X.Y.Z)")
    ap.add_argument("--optional", action="append", metavar="NAME", help="--dev: a package file that is not required")
    ap.add_argument("--release", metavar="N", help="release N (v1, v2, ...): refuse unless the exe is N.0.0, "
                                                   "which is what games.json then carries")
    ap.add_argument("--version", help="refuse unless the exe's version resource says exactly this")
    ap.add_argument("--games-in", help="the games list source (default: site/games.json.in, or "
                                       "site/dev-games.json.in with --dev)")
    ap.add_argument("--out", help="where games.json is written (default: site/games.json); with --dev the folder "
                                  "games.json and the manifests are written to (default: site/dev)")
    ap.add_argument("--dry-run", action="store_true", help="check and write games.json; upload nothing")
    a = ap.parse_args(argv)
    a.exe_given = a.exe is not None
    if not a.dev:
        extra = [f for f, v in (("--no-exe", a.no_exe), ("--package", a.package),
                                ("--package-version", a.package_version), ("--optional", a.optional)) if v]
        if extra:
            print(f"REFUSED: {', '.join(extra)} only go with --dev", file=sys.stderr)
            return 1
    a.exe = a.exe or (DEV_DEFAULT_EXE if a.dev else DEFAULT_EXE)
    a.games_in = a.games_in or (DEV_GAMES_IN if a.dev else GAMES_IN)
    if not a.dev:
        a.out = a.out or os.path.join(REPO, "site", "games.json")
    try:
        publish_dev(a) if a.dev else publish(a)
    except Refused as e:
        print(f"REFUSED: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
