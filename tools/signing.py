"""ed25519 signatures on what coopmods.com serves (#21, docs/SPEC.md "Signatures").

    python tools/signing.py sign FILE...     # writes FILE.sig beside each FILE
    python tools/signing.py verify FILE...   # checks each FILE.sig against the keys the launcher trusts
    python tools/signing.py pubkey           # the public key of the private key in use

The format, shared with mewgenics-coop's update manifest (Magto/mewgenics-coop#553): `FILE.sig` is the raw
64-byte ed25519 signature (RFC 8032) over the exact bytes of FILE, written as 128 lowercase hex digits and a
newline. FILE itself is not changed, so a reader that does not check signatures reads it as before.

A signature covers bytes, not an address, so what this repo signs also names itself inside the signed bytes (#23):
a top-level "signed_as" string, "games.json", "dev games.json" or "<game id> manifest" (games_json_signed_as,
manifest_signed_as below; src/signature.h has the launcher's copy of the rule).

Each key in src/trusted_keys.h may sign only the names its rows allow: "*" is every name (Martin's key), a game's
own key gets "<id> manifest" only. `sign` refuses a file whose "signed_as" the key may not sign, and a file with no
"signed_as" unless the key may sign every name; `verify` says BAD for the same, as the launcher refuses it.

The private key is 32 raw bytes, the ed25519 seed, in the file COOPMODS_SIGNING_KEY names, else in
~/.config/coopmods/signing.key. It lives on Martin's machine only, mode 600, with an offline backup in his
password manager: never in a repo, never on the server, never printed. Without it nothing is signed.

A key is only used when its public key is in src/trusted_keys.h -- the list compiled into FFB Co-op.exe --
so nothing is ever signed that the launcher would refuse. Needs the `cryptography` package.
"""
import json, os, re, stat, sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KEY_ENV = "COOPMODS_SIGNING_KEY"
DEFAULT_KEY = os.path.join("~", ".config", "coopmods", "signing.key")
TRUSTED_KEYS_H = os.path.join("src", "trusted_keys.h")
SIG_SUFFIX = ".sig"
_ROW = re.compile(r'\{\s*"([0-9a-fA-F]{64})"\s*,\s*"([^"]+)"\s*\}')   # {"<key>", "<name or *>"}
_SIG = re.compile(r"[0-9a-fA-F]{128}")


class SigningError(Exception):
    """A reason not to sign. The message never contains key material."""


def _ed25519():
    try:
        from cryptography.hazmat.primitives.asymmetric import ed25519
        from cryptography.hazmat.primitives import serialization
    except ImportError:
        raise SigningError("the Python package `cryptography` is missing (pip install cryptography)")
    return ed25519, serialization


def key_path():
    """The private key's path: COOPMODS_SIGNING_KEY when set, else ~/.config/coopmods/signing.key."""
    return os.path.expanduser(os.environ.get(KEY_ENV) or DEFAULT_KEY)


def load_private_key(path=None):
    """-> the Ed25519PrivateKey in `path` (default key_path()). Refuses a missing or unreadable file, one that
    is not exactly 32 bytes, and -- off Windows -- one that anyone but its owner may read."""
    path = path or key_path()
    if not os.path.isfile(path):
        raise SigningError(f"no signing key at {path} (set {KEY_ENV} or put it at {DEFAULT_KEY})")
    if os.name != "nt" and os.stat(path).st_mode & (stat.S_IRWXG | stat.S_IRWXO):
        raise SigningError(f"{path} is readable by others than its owner -- chmod 600 it")
    try:
        with open(path, "rb") as f:
            seed = f.read()
    except OSError as e:
        raise SigningError(f"cannot read the signing key {path}: {e.strerror}")
    if len(seed) != 32:
        raise SigningError(f"{path} is not a raw 32-byte ed25519 key ({len(seed)} bytes)")
    ed25519, _ = _ed25519()
    return ed25519.Ed25519PrivateKey.from_private_bytes(seed)


def public_hex(private_key):
    _, ser = _ed25519()
    return private_key.public_key().public_bytes(ser.Encoding.Raw, ser.PublicFormat.Raw).hex()


def _rows(repo):
    with open(os.path.join(repo, TRUSTED_KEYS_H), encoding="utf-8") as f:
        rows = [(k.lower(), name) for k, name in _ROW.findall(f.read())]
    if not rows:
        raise SigningError(f"{TRUSTED_KEYS_H} lists no key")
    return rows


def trusted_keys(repo=REPO):
    """-> the public keys (lowercase hex) FFB Co-op.exe trusts, read from src/trusted_keys.h, each once."""
    return list(dict.fromkeys(k for k, _ in _rows(repo)))


def key_rules(repo=REPO):
    """-> src/trusted_keys.h's rows, [(public key hex, name or "*")]: which names each key may sign (#23)."""
    return _rows(repo)


def may_sign(key_hex, name, rules=None):
    """True when one of `key_hex`'s rows says "*" or exactly `name` (src/signature.cpp, check_may_sign). `name` None
    (a file with no "signed_as") is allowed only to a key that may sign every name."""
    rules = key_rules() if rules is None else rules
    return any(k == key_hex.lower() and (n == "*" or n == name) for k, n in rules)


def signed_as_of(data):
    """-> the "signed_as" string of a JSON object's bytes, or None (not JSON, not an object, or none)."""
    try:
        doc = json.loads(data.decode("utf-8"))
    except (UnicodeDecodeError, ValueError):
        return None
    name = doc.get("signed_as") if isinstance(doc, dict) else None
    return name if isinstance(name, str) else None


def load_trusted_key(repo=REPO, path=None):
    """The private key, refused unless the launcher trusts its public key."""
    key = load_private_key(path)
    pub = public_hex(key)
    if pub not in trusted_keys(repo):
        raise SigningError(f"the signing key's public key {pub} is not in {TRUSTED_KEYS_H}: "
                           "FFB Co-op.exe would refuse everything it signs")
    return key


def sign_bytes(data, private_key):
    """-> the text of the .sig file for `data`."""
    return private_key.sign(data).hex() + "\n"


def signer_of(data, sig_text, keys_hex):
    """-> the one of `keys_hex` whose valid signature of exactly `data` `sig_text` (a .sig file's text) is, or None.
    The same rule as the launcher (src/signature.cpp): 128 hex digits, then only whitespace."""
    if isinstance(sig_text, bytes):
        try:
            sig_text = sig_text.decode("ascii")
        except UnicodeDecodeError:
            return None
    sig_text = sig_text.rstrip(" \t\r\n")
    if not _SIG.fullmatch(sig_text):
        return None
    ed25519, _ = _ed25519()
    from cryptography.exceptions import InvalidSignature
    for k in keys_hex:
        try:
            ed25519.Ed25519PublicKey.from_public_bytes(bytes.fromhex(k)).verify(bytes.fromhex(sig_text), data)
            return k
        except (InvalidSignature, ValueError):
            continue
    return None


def verify_bytes(data, sig_text, keys_hex):
    """True when `sig_text` (a .sig file's text) is a valid signature of exactly `data` by one of `keys_hex`."""
    return signer_of(data, sig_text, keys_hex) is not None


def signature_url(url):
    """-> where a launcher fetches the .sig of the file at `url`: its query or fragment dropped, ".sig" appended
    (src/signature.cpp, signature_url)."""
    return url.split("?", 1)[0].split("#", 1)[0] + SIG_SUFFIX


def games_json_signed_as(channel="public"):
    """The "signed_as" of a channel's games.json (#23): "games.json", or "<channel> games.json"."""
    return "games.json" if channel == "public" else f"{channel} games.json"


def manifest_signed_as(game_id):
    """The "signed_as" of a game's manifest (#23): "<games[].id> manifest", e.g. "mewgenics manifest"."""
    return f"{game_id} manifest"


def check_may_sign(data, private_key, rules=None):
    """Refuse to sign `data` with a key that may not sign its "signed_as" (#23): the launcher would refuse it."""
    pub, name = public_hex(private_key), signed_as_of(data)
    if not may_sign(pub, name, rules):
        what = f'"signed_as": "{name}"' if name is not None else 'no "signed_as"'
        raise SigningError(f"the key {pub} may not sign a file with {what} ({TRUSTED_KEYS_H} lists the names "
                           "each key may sign)")


def sign_file(path, private_key, rules=None):
    """Writes `path`.sig beside `path`; -> its path. Refuses a file the key may not sign (check_may_sign)."""
    with open(path, "rb") as f:
        data = f.read()
    check_may_sign(data, private_key, rules)
    out = path + SIG_SUFFIX
    with open(out, "w", encoding="ascii", newline="\n") as f:
        f.write(sign_bytes(data, private_key))
    return out


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if not argv or argv[0] not in ("sign", "verify", "pubkey"):
        print(__doc__.split("\n\n")[1], file=sys.stderr)
        return 2
    try:
        if argv[0] == "pubkey":
            print(public_hex(load_private_key()))
            return 0
        if argv[0] == "sign":
            key = load_trusted_key()
            for p in argv[1:]:
                print("signed", sign_file(p, key))
            return 0
        bad = 0
        for p in argv[1:]:
            with open(p, "rb") as f:
                data = f.read()
            try:
                with open(p + SIG_SUFFIX, "rb") as f:
                    sig = f.read()
            except OSError:
                sig = b""
            signer = signer_of(data, sig, trusted_keys())
            ok = signer is not None and may_sign(signer, signed_as_of(data))
            print(("ok     " if ok else "BAD    ") + p)
            bad += not ok
        return 1 if bad else 0
    except SigningError as e:
        print(f"REFUSED: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
