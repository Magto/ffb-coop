"""Publish FFB Co-op.exe and games.json to coopmods.com (SHIP_CMD, #7).

    python tools/publish.py [--exe "build/Release/FFB Co-op.exe"] [--version X.Y.Z] [--dry-run]

What it does, in order, and what makes it refuse:

1. Runs tools/doc_rules.sh and tools/scan_rules.sh; refuses on a non-zero exit, or when either is missing
   (a --dry-run only warns about a missing one).
2. Reads the exe: its sha256, its size and its version from the version resource (VS_FIXEDFILEINFO,
   FileVersion MAJOR.MINOR.PATCH). Refuses when there is no version resource, when FileVersion and
   ProductVersion disagree, or when --version is given and differs from the exe's.
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
"""
import argparse, hashlib, json, os, re, struct, subprocess, sys, tempfile, urllib.error, urllib.request

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


def launcher_block(data):
    return {"version": exe_version(data), "url": LAUNCHER_URL,
            "sha256": hashlib.sha256(data).hexdigest(), "size": len(data)}


# ---- games.json: the rules of docs/SPEC.md, "Validation, field by field" -----------------------------

VERSION_RE = re.compile(r"(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)")
HEX64_RE = re.compile(r"[0-9a-fA-F]{64}")
ID_RE = re.compile(r"[a-z0-9-]{1,32}")
RESERVED = ({"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$", "CLOCK$"}
            | {f"{d}{i}" for d in ("COM", "LPT") for i in "123456789\u00b9\u00b2\u00b3"})   # ¹ ² ³


def version_key(v):
    return tuple(int(p) for p in v.split("."))


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


def build_games_json(games_in, exe_bytes):
    """-> the games.json document: the launcher block from `exe_bytes`, the games from `games_in` (the
    parsed site/games.json.in). Refuses rather than return a file the launcher would reject."""
    if not isinstance(games_in, dict) or games_in.get("schema") != 1:
        raise Refused("site/games.json.in must be an object with \"schema\": 1")
    if "launcher" in games_in:
        raise Refused("site/games.json.in must not carry a launcher block -- it is written from the exe")
    doc = {"schema": 1, "launcher": launcher_block(exe_bytes), "games": games_in.get("games")}
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


def dumps(doc):
    return json.dumps(doc, indent=2) + "\n"


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


def remote_put(local, remote_name, sha256):
    """scp to a temporary name, check the sha256 there, then move into place (a rename, so a reader never
    sees half a file). `remote_name` is relative to REMOTE and may contain a space."""
    tmp = f"{REMOTE}/.upload-{sha256[:16]}"
    run(["scp", "-q", local, f"{HOST}:{tmp}"])
    r = run(["ssh", "-o", "ConnectTimeout=15", HOST, f"sha256sum {tmp}"])
    got = r.stdout.split()[0] if r.stdout.split() else ""
    if got.lower() != sha256.lower():
        run(["ssh", HOST, f"rm -f {tmp}"], check=False)
        raise Refused(f"{remote_name} arrived with sha256 {got or '?'}, expected {sha256}")
    run(["ssh", HOST, f"mv -f {tmp} '{REMOTE}/{remote_name}'"])


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
    ver = doc["launcher"]["version"]
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
    ap.add_argument("--exe", default=DEFAULT_EXE, help="the built launcher (default: build/Release/FFB Co-op.exe)")
    ap.add_argument("--version", help="refuse unless the exe's version resource says exactly this")
    ap.add_argument("--games-in", default=GAMES_IN, help="the games list source (default: site/games.json.in)")
    ap.add_argument("--out", default=os.path.join(REPO, "site", "games.json"), help="where games.json is written")
    ap.add_argument("--dry-run", action="store_true", help="check and write games.json; upload nothing")
    a = ap.parse_args(argv)
    try:
        publish(a)
    except Refused as e:
        print(f"REFUSED: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
