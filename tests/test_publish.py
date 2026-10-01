"""Unit tests for tools/publish.py and tools/signing.py: the games.json writer, its signature and their
refusals. No network, no exe needed, and never the real signing key: setUpModule points COOPMODS_SIGNING_KEY at
RFC 8032's TEST 1 key and makes that the one trusted key.

    python -m unittest discover -s tests -p "test_*.py"
"""
import copy, hashlib, json, os, struct, sys, tempfile, unittest
from unittest import mock

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tools"))
import publish  # noqa: E402
import signing  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# RFC 8032 section 7.1: TEST 1 (the test key) signs the empty message, TEST 2 is a key nobody trusts.
TEST_SEED = bytes.fromhex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60")
TEST_PUB = "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"
RFC_SIG_EMPTY = ("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b4"
                 "6bd25bf5f0595bbe24655141438e7a100b")
OTHER_SEED = bytes.fromhex("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb")
OTHER_PUB = "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c"
# The key generated for coopmods on 2026-10-01, as docs/SPEC.md "Signatures" names it.
COOPMODS_PUB = "43706304f0fae090f7a2afd96e06d207ab193ea52a527d6576073ce281b30a59"

REAL_TRUSTED_KEYS = signing.trusted_keys
_module = {}


def write_key(path, seed, mode=0o600):
    with open(path, "wb") as f:
        f.write(seed)
    os.chmod(path, mode)
    return path


def setUpModule():
    _module["tmp"] = tempfile.TemporaryDirectory()
    key = write_key(os.path.join(_module["tmp"].name, "signing.key"), TEST_SEED)
    _module["patches"] = [mock.patch.dict(os.environ, {signing.KEY_ENV: key}),
                          mock.patch.object(signing, "trusted_keys", return_value=[TEST_PUB])]
    for p in _module["patches"]: p.start()


def tearDownModule():
    for p in _module["patches"]: p.stop()
    _module["tmp"].cleanup()


def load(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def fake_exe(file_ver=(1, 2, 3, 0), prod_ver=None, pad=b"MZ" + b"\0" * 200):
    """Bytes with a VS_FIXEDFILEINFO in them, laid out by hand (not by the code under test)."""
    prod_ver = prod_ver or file_ver
    ms = lambda v: (v[0] << 16) | v[1]
    ls = lambda v: (v[2] << 16) | v[3]
    info = struct.pack("<13I", 0xFEEF04BD, 0x00010000, ms(file_ver), ls(file_ver),
                       ms(prod_ver), ls(prod_ver), 0x3F, 0, 0x40004, 1, 0, 0, 0)
    return pad + info + b"\0" * 64


GAMES_IN = {"schema": 1, "games": [{
    "id": "mewgenics", "name": "Mewgenics", "exe": "Mewgenics.exe", "steam_appid": 0,
    "manifest": "https://mewgenics.coopmods.com/update/manifest.json", "launcher": "mewcoop_loader.exe"}]}


class ExeVersion(unittest.TestCase):
    def test_reads_major_minor_patch(self):
        self.assertEqual(publish.exe_version(fake_exe((1, 10, 7, 0))), "1.10.7")

    def test_build_part_ignored(self):
        self.assertEqual(publish.exe_version(fake_exe((2, 0, 1, 99))), "2.0.1")

    def test_no_version_resource_refused(self):
        with self.assertRaises(publish.Refused):
            publish.exe_version(b"MZ" + b"\0" * 500)

    def test_file_and_product_version_disagree_refused(self):
        with self.assertRaises(publish.Refused):
            publish.exe_version(fake_exe((1, 0, 0, 0), (1, 0, 1, 0)))


class Writer(unittest.TestCase):
    def test_launcher_block_is_the_exe_bytes(self):
        data = fake_exe((1, 0, 0, 0))
        doc = publish.build_games_json(copy.deepcopy(GAMES_IN), data)
        self.assertEqual(doc["schema"], 1)
        self.assertEqual(doc["launcher"], {
            "version": "1.0.0", "url": "https://coopmods.com/launcher/FFB%20Co-op.exe",
            "sha256": hashlib.sha256(data).hexdigest(), "size": len(data)})
        self.assertEqual(doc["games"], GAMES_IN["games"])
        self.assertEqual(publish.validate_games_json(doc), [])

    def test_checked_in_source_is_valid(self):
        games_in = load(os.path.join(REPO, "site", "games.json.in"))
        doc = publish.build_games_json(games_in, fake_exe())
        self.assertEqual(publish.validate_games_json(doc), [])

    def test_source_with_launcher_block_refused(self):
        g = copy.deepcopy(GAMES_IN); g["launcher"] = {"version": "9.9.9"}
        with self.assertRaises(publish.Refused):
            publish.build_games_json(g, fake_exe())

    def test_empty_games_refused(self):
        g = copy.deepcopy(GAMES_IN); g["games"] = []
        with self.assertRaises(publish.Refused):
            publish.build_games_json(g, fake_exe())

    def test_invalid_game_refused(self):
        for key, bad in (("exe", "..\\Mewgenics.exe"), ("exe", "CON.exe"), ("launcher", "loader.dll"),
                         ("id", "Mew"), ("steam_appid", -1), ("manifest", "http://x/m.json"), ("name", "")):
            g = copy.deepcopy(GAMES_IN); g["games"][0][key] = bad
            with self.subTest(key=key, bad=bad), self.assertRaises(publish.Refused):
                publish.build_games_json(g, fake_exe())

    def test_exe_suffix_any_letter_case_accepted(self):
        # Martin, 2026-09-25 12:53 (#3): "yes to the letter case" -- .exe in any case.
        for key, name in (("launcher", "mewcoop_loader.EXE"), ("launcher", "Loader.Exe"), ("exe", "MEWGENICS.EXE")):
            g = copy.deepcopy(GAMES_IN); g["games"][0][key] = name
            with self.subTest(key=key, name=name):
                self.assertEqual(publish.build_games_json(g, fake_exe())["games"][0][key], name)

    def test_duplicate_exe_any_case_refused(self):
        g = copy.deepcopy(GAMES_IN)
        g["games"].append(dict(g["games"][0], id="other", exe="MEWGENICS.EXE"))
        with self.assertRaises(publish.Refused):
            publish.build_games_json(g, fake_exe())


class PlainName(unittest.TestCase):
    def test_rule(self):
        for ok in ("Mewgenics.exe", "mewcoop_loader.exe", "a b.exe"):
            self.assertIsNone(publish.plain_name_problem(ok), ok)
        for bad in ("", ".", "..", "a/b.exe", "a\\b.exe", "c:x.exe", "a..b.exe", " a.exe", "a.exe ",
                    "a.", "NUL", "com1.exe", "LPT9.txt", "a\x01.exe", "x" * 256):
            self.assertIsNotNone(publish.plain_name_problem(bad), repr(bad))

    def test_rule_after_16(self):
        # docs/SPEC.md "Network file-name rule" as PR #16 wrote it, the rule src/plain_name.cpp enforces.
        for ok in ("com10.exe", "COM0.exe", "console.exe", "nul_.exe", "clock.exe", "x" * 255,
                   "é" * 255):
            self.assertIsNone(publish.plain_name_problem(ok), repr(ok))
        for bad in ("a<b.exe", "a>b.exe", 'a"b.exe', "a|b.exe", "a?.exe", "a*.exe",   # < > " | ? *
                    "a\x00.exe", "a\x1f.exe", "a\x7f.exe",                             # 0x00-0x1F, 0x7F
                    "CONIN$", "conin$.exe", "CONOUT$.exe", "Clock$.txt",               # the $ devices
                    "COM¹.exe", "com²", "COM³.txt",                     # superscript 1 2 3
                    "LPT¹.exe", "lpt².exe", "LPT³",
                    "nul .txt", "CON  .exe", "com1 .exe", "clock$ .exe",               # stem's trailing spaces
                    "é" * 256):
            self.assertIsNotNone(publish.plain_name_problem(bad), repr(bad))


class AgainstLive(unittest.TestCase):
    def doc(self, ver, data):
        return publish.build_games_json(copy.deepcopy(GAMES_IN), fake_exe(ver, pad=data))

    def test_nothing_live_accepts(self):
        publish.check_against_live(self.doc((1, 0, 0, 0), b"a"), None)

    def test_newer_accepts(self):
        publish.check_against_live(self.doc((1, 0, 1, 0), b"b"), self.doc((1, 0, 0, 0), b"a"))

    def test_same_version_same_bytes_accepts(self):
        publish.check_against_live(self.doc((1, 0, 0, 0), b"a"), self.doc((1, 0, 0, 0), b"a"))

    def test_older_refused(self):
        with self.assertRaises(publish.Refused):
            publish.check_against_live(self.doc((1, 9, 0, 0), b"b"), self.doc((1, 10, 0, 0), b"a"))

    def test_same_version_different_bytes_refused(self):
        with self.assertRaises(publish.Refused):
            publish.check_against_live(self.doc((1, 0, 0, 0), b"b"), self.doc((1, 0, 0, 0), b"a"))


class DryRun(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.exe = os.path.join(self.tmp.name, "FFB Co-op.exe")
        self.data = fake_exe((1, 0, 0, 0))
        with open(self.exe, "wb") as f:
            f.write(self.data)
        self.out = os.path.join(self.tmp.name, "games.json")
        self.patches = [mock.patch.object(publish, "run_gates"),
                        mock.patch.object(publish, "fetch_live_games", return_value=None),
                        mock.patch.object(publish, "run", side_effect=AssertionError("dry run touched the server"))]
        for p in self.patches: p.start()

    def tearDown(self):
        for p in self.patches: p.stop()
        self.tmp.cleanup()

    def test_writes_games_json_and_uploads_nothing(self):
        self.assertEqual(publish.main(["--exe", self.exe, "--out", self.out, "--dry-run"]), 0)
        doc = load(self.out)
        self.assertEqual(doc["launcher"]["sha256"], hashlib.sha256(self.data).hexdigest())
        self.assertEqual(doc["launcher"]["size"], len(self.data))

    def test_version_flag_disagreeing_with_exe_refused(self):
        self.assertEqual(publish.main(["--exe", self.exe, "--out", self.out, "--dry-run", "--version", "1.0.1"]), 1)
        self.assertFalse(os.path.exists(self.out))

    def test_missing_exe_refused(self):
        self.assertEqual(publish.main(["--exe", self.exe + ".nope", "--out", self.out, "--dry-run"]), 1)


class Release(unittest.TestCase):
    """Release N is "N.0.0" (#24, docs/SPEC.md "Versions"); v1 is the first after the field's 0.1.0."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.exe = os.path.join(self.tmp.name, "FFB Co-op.exe")
        self.out = os.path.join(self.tmp.name, "games.json")
        self.live = None
        self.patches = [mock.patch.object(publish, "run_gates"),
                        mock.patch.object(publish, "fetch_live_games", side_effect=lambda: self.live),
                        mock.patch.object(publish, "run", side_effect=AssertionError("dry run touched the server"))]
        for p in self.patches: p.start()

    def tearDown(self):
        for p in self.patches: p.stop()
        self.tmp.cleanup()

    def exe_says(self, ver):
        with open(self.exe, "wb") as f:
            f.write(fake_exe(ver))

    def main(self, *extra):
        return publish.main(["--exe", self.exe, "--out", self.out, "--dry-run", *extra])

    def test_release_number_is_n_0_0(self):
        self.assertEqual(publish.release_version("1"), "1.0.0")
        self.assertEqual(publish.release_version("2"), "2.0.0")
        self.assertEqual(publish.release_version("10"), "10.0.0")

    def test_not_a_release_number_refused(self):
        for bad in ("0", "01", "-1", "v1", "1.0", "1.0.0", "", " 1", "1 "):
            with self.assertRaises(publish.Refused, msg=repr(bad)):
                publish.release_version(bad)

    def test_release_1_writes_1_0_0(self):
        self.exe_says((1, 0, 0, 0))
        self.assertEqual(self.main("--release", "1"), 0)
        self.assertEqual(load(self.out)["launcher"]["version"], "1.0.0")

    def test_release_disagreeing_with_exe_refused(self):
        self.exe_says((1, 0, 0, 0))
        self.assertEqual(self.main("--release", "2"), 1)
        self.assertFalse(os.path.exists(self.out))

    def test_exe_not_a_release_version_refused(self):
        for ver in ((0, 1, 0, 0), (1, 0, 1, 0), (1, 1, 0, 0)):
            self.exe_says(ver)
            self.assertEqual(self.main(), 1, ver)
            self.assertFalse(os.path.exists(self.out), ver)

    def test_v1_over_the_live_0_1_0_accepted(self):
        self.live = publish.build_games_json(copy.deepcopy(GAMES_IN), fake_exe((0, 1, 0, 0), pad=b"old"))
        self.exe_says((1, 0, 0, 0))
        self.assertEqual(self.main("--release", "1"), 0)
        self.assertEqual(load(self.out)["launcher"]["version"], "1.0.0")

    def test_release_older_than_live_refused(self):
        self.live = publish.build_games_json(copy.deepcopy(GAMES_IN), fake_exe((2, 0, 0, 0), pad=b"v2"))
        self.exe_says((1, 0, 0, 0))
        self.assertEqual(self.main("--release", "1"), 1)
        self.assertFalse(os.path.exists(self.out))


class FakeServer:
    """Stands in for ssh/scp and for https: a dict of remote path -> bytes. It does what the commands
    say (scp copies the local file, `sha256sum`/`mv -f`/`rm -f`/`mkdir -p` act on the dict) and records
    every command, so a test sees what publish sent and in which order. `corrupt` makes scp deliver
    other bytes; `stale` makes https answer other bytes than the files on disk."""

    def __init__(self):
        self.files, self.cmds, self.corrupt, self.stale = {}, [], False, False

    def run(self, cmd, check=True):
        self.cmds.append(cmd)
        out, rc = "", 0
        if cmd[0] == "scp":
            local, target = cmd[-2], cmd[-1]
            with open(local, "rb") as f:
                data = f.read()
            self.files[target.split(":", 1)[1]] = data + (b"x" if self.corrupt else b"")
        else:
            shell = cmd[-1]
            if shell.startswith("sha256sum "):
                path = shell.split(" ", 1)[1]
                out = f"{hashlib.sha256(self.files[path]).hexdigest()}  {path}\n"
            elif shell.startswith("mv -f "):
                src, dst = shell[len("mv -f "):].split(" ", 1)
                self.files[dst.strip("'")] = self.files.pop(src)
            elif shell.startswith("rm -f "):
                self.files.pop(shell[len("rm -f "):], None)
            elif not shell.startswith("mkdir -p "):
                raise AssertionError(f"unexpected command {cmd}")
        if check and rc:
            raise publish.Refused("fake command failed")
        return mock.Mock(returncode=rc, stdout=out, stderr="")

    def fetch(self, url, timeout=20):
        path = {publish.URL + "/games.json": publish.REMOTE + "/games.json",
                publish.SIG_URL: publish.REMOTE + "/games.json.sig",
                publish.LAUNCHER_URL: publish.REMOTE + "/launcher/" + publish.EXE_NAME}[url]
        data = self.files.get(path)
        return None if data is None else (b"stale " + data if self.stale else data)

    def index(self, pred):
        return next(i for i, c in enumerate(self.cmds) if pred(c))


class Upload(unittest.TestCase):
    """A real (not --dry-run) publish against FakeServer: the upload order, the temporary names, the
    server-side sha256 check and the cookie-free verify."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.exe = os.path.join(self.tmp.name, "FFB Co-op.exe")
        self.data = fake_exe((1, 0, 0, 0))
        with open(self.exe, "wb") as f:
            f.write(self.data)
        self.out = os.path.join(self.tmp.name, "games.json")
        self.server = FakeServer()
        self.patches = [mock.patch.object(publish, "run_gates"),
                        mock.patch.object(publish, "fetch_live_games", return_value=None),
                        mock.patch.object(publish, "run", side_effect=self.server.run),
                        mock.patch.object(publish, "fetch", side_effect=self.server.fetch)]
        for p in self.patches: p.start()

    def tearDown(self):
        for p in self.patches: p.stop()
        self.tmp.cleanup()

    def main(self):
        return publish.main(["--exe", self.exe, "--out", self.out])

    def test_exe_goes_up_before_games_json_under_temporary_names(self):
        self.assertEqual(self.main(), 0)
        s = self.server
        scps = [c for c in s.cmds if c[0] == "scp"]
        self.assertEqual(len(scps), 3)   # the exe, games.json.sig, games.json (#21)
        for c in scps:
            self.assertTrue(c[-1].split(":", 1)[1].startswith(publish.REMOTE + "/.upload-"), c)
        exe_mv = s.index(lambda c: c[-1].startswith("mv -f ") and c[-1].endswith(f"/launcher/{publish.EXE_NAME}'"))
        sig_mv = s.index(lambda c: c[-1].startswith("mv -f ") and c[-1].endswith("/games.json.sig'"))
        games_scp = s.index(lambda c: c[0] == "scp" and c[-2] == self.out)
        games_mv = s.index(lambda c: c[-1].startswith("mv -f ") and c[-1].endswith("/games.json'"))
        self.assertLess(exe_mv, games_scp)
        self.assertLess(sig_mv, games_mv)
        self.assertLess(games_scp, games_mv)
        self.assertEqual(s.files[publish.REMOTE + "/launcher/" + publish.EXE_NAME], self.data)
        self.assertEqual(json.loads(s.files[publish.REMOTE + "/games.json"]), load(self.out))
        self.assertTrue(signing.verify_bytes(s.files[publish.REMOTE + "/games.json"],
                                             s.files[publish.REMOTE + "/games.json.sig"], [TEST_PUB]))
        self.assertEqual([p for p in s.files if ".upload-" in p], [])

    def test_server_sha256_disagreeing_refused_and_nothing_moved(self):
        self.server.corrupt = True
        self.assertEqual(self.main(), 1)
        self.assertFalse([c for c in self.server.cmds if c[-1].startswith("mv ")])
        self.assertFalse([c for c in self.server.cmds if c[0] == "scp" and c[-2] == self.out])

    def test_live_answering_other_bytes_refused(self):
        self.server.stale = True
        self.assertEqual(self.main(), 1)

    def test_verify_live_refuses_other_bytes(self):
        doc = publish.build_games_json(copy.deepcopy(GAMES_IN), self.data)
        key = signing.load_private_key()
        sig = signing.sign_bytes(dumps_bytes(doc), key).encode()
        good = {publish.URL + "/games.json": dumps_bytes(doc), publish.LAUNCHER_URL: self.data, publish.SIG_URL: sig}
        publish.fetch.side_effect = lambda url, timeout=20: good[url]
        publish.verify_live(doc, [TEST_PUB])   # the right bytes pass
        other_key_sig = signing.sign_bytes(dumps_bytes(doc), signing._ed25519()[0].Ed25519PrivateKey
                                           .from_private_bytes(OTHER_SEED)).encode()
        for url, other in ((publish.LAUNCHER_URL, self.data + b"x"), (publish.LAUNCHER_URL, None),
                           (publish.URL + "/games.json", b'{"schema": 1}'), (publish.URL + "/games.json", None),
                           (publish.SIG_URL, None), (publish.SIG_URL, other_key_sig),
                           (publish.SIG_URL, publish.PLACEHOLDER)):
            answers = dict(good, **{url: other})
            publish.fetch.side_effect = lambda u, timeout=20, a=answers: a[u]
            with self.subTest(url=url, other=other), self.assertRaises(publish.Refused):
                publish.verify_live(doc, [TEST_PUB])

    def test_live_games_json_unreadable_refused_before_upload(self):
        publish.fetch_live_games.side_effect = OSError("no route to host")
        self.assertEqual(self.main(), 1)
        self.assertEqual(self.server.cmds, [])


def dumps_bytes(doc):
    return publish.dumps(doc).encode("utf-8")


class Gates(unittest.TestCase):
    """run_gates for real, against a temporary tree standing in for the repo."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        os.makedirs(os.path.join(self.tmp.name, "tools"))
        self.exe = os.path.join(self.tmp.name, "FFB Co-op.exe")
        with open(self.exe, "wb") as f:
            f.write(fake_exe((1, 0, 0, 0)))
        self.out = os.path.join(self.tmp.name, "games.json")
        self.run = mock.Mock(side_effect=AssertionError("published past a failing gate"))
        self.patches = [mock.patch.object(publish, "REPO", self.tmp.name),
                        mock.patch.object(publish, "fetch_live_games", return_value=None),
                        mock.patch.object(publish, "fetch", side_effect=AssertionError("fetched")),
                        mock.patch.object(publish, "check_sig_route"),   # its own tests are in Signing
                        mock.patch.object(publish, "run", self.run)]
        for p in self.patches: p.start()

    def tearDown(self):
        for p in self.patches: p.stop()
        self.tmp.cleanup()

    def gate(self, name, body):
        with open(os.path.join(self.tmp.name, "tools", name), "w", newline="\n") as f:
            f.write("#!/bin/bash\n" + body + "\n")

    def main(self, *extra):
        return publish.main(["--exe", self.exe, "--out", self.out, *extra])

    def test_failing_gate_refused(self):
        self.gate("doc_rules.sh", "exit 0")
        self.gate("scan_rules.sh", "echo 'x.cpp:1: bad'; exit 3")
        self.assertEqual(self.main(), 1)
        self.run.assert_not_called()
        self.assertFalse(os.path.exists(self.out))

    def test_failing_gate_refused_on_dry_run_too(self):
        self.gate("doc_rules.sh", "exit 3")
        self.gate("scan_rules.sh", "exit 0")
        self.assertEqual(self.main("--dry-run"), 1)

    def test_missing_gate_refused(self):
        self.gate("doc_rules.sh", "exit 0")
        self.assertEqual(self.main(), 1)
        self.run.assert_not_called()
        self.assertFalse(os.path.exists(self.out))

    def test_passing_gates_let_it_through(self):
        # the control: the same tree with both gates green gets past the gates (to the fake upload)
        self.gate("doc_rules.sh", "exit 0")
        self.gate("scan_rules.sh", "exit 0")
        self.run.side_effect = publish.Refused("stop at the upload")
        self.assertEqual(self.main(), 1)
        self.run.assert_called()


class Signing(unittest.TestCase):
    """tools/signing.py and the signing step of publish (#21): the key it reads, what it refuses, and the
    .sig it writes. Expected signatures are RFC 8032's, not output of this code."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.exe = os.path.join(self.tmp.name, "FFB Co-op.exe")
        self.data = fake_exe((1, 0, 0, 0))
        with open(self.exe, "wb") as f:
            f.write(self.data)
        self.out = os.path.join(self.tmp.name, "games.json")
        self.server = FakeServer()
        self.patches = [mock.patch.object(publish, "run_gates"),
                        mock.patch.object(publish, "fetch_live_games", return_value=None),
                        mock.patch.object(publish, "run", side_effect=self.server.run),
                        mock.patch.object(publish, "fetch", side_effect=self.server.fetch)]
        for p in self.patches: p.start()

    def tearDown(self):
        for p in self.patches: p.stop()
        self.tmp.cleanup()

    def key_env(self, path):
        return mock.patch.dict(os.environ, {signing.KEY_ENV: path})

    def assert_nothing_uploaded(self):
        self.assertEqual([c for c in self.server.cmds if c[0] in ("scp", "ssh")], [])

    def test_rfc8032_vector(self):
        key = signing.load_private_key()
        self.assertEqual(signing.public_hex(key), TEST_PUB)
        self.assertEqual(signing.sign_bytes(b"", key), RFC_SIG_EMPTY + "\n")

    def test_valid_missing_wrong_key_tampered(self):
        data = dumps_bytes(publish.build_games_json(copy.deepcopy(GAMES_IN), self.data))
        good = signing.sign_bytes(data, signing.load_private_key())
        other = signing.sign_bytes(data, signing._ed25519()[0].Ed25519PrivateKey.from_private_bytes(OTHER_SEED))
        self.assertTrue(signing.verify_bytes(data, good, [TEST_PUB]))                 # valid
        self.assertTrue(signing.verify_bytes(data, good.encode(), [TEST_PUB]))
        self.assertFalse(signing.verify_bytes(data, "", [TEST_PUB]))                  # missing
        self.assertFalse(signing.verify_bytes(data, other, [TEST_PUB]))               # wrong key
        self.assertTrue(signing.verify_bytes(data, other, [TEST_PUB, OTHER_PUB]))     # ... until it is trusted
        for at in (0, len(data) // 2, len(data) - 1):                                 # tampered byte
            t = bytearray(data)
            t[at] ^= 1
            self.assertFalse(signing.verify_bytes(bytes(t), good, [TEST_PUB]), at)
        self.assertFalse(signing.verify_bytes(data, good[:-2], [TEST_PUB]))
        self.assertFalse(signing.verify_bytes(data, publish.PLACEHOLDER, [TEST_PUB]))

    def test_real_trusted_keys_file(self):
        self.assertEqual(REAL_TRUSTED_KEYS(REPO), [COOPMODS_PUB])

    def test_dry_run_signs_and_the_sig_verifies(self):
        self.assertEqual(publish.main(["--exe", self.exe, "--out", self.out, "--dry-run"]), 0)
        with open(self.out, "rb") as f, open(self.out + ".sig", encoding="ascii") as g:
            data, sig = f.read(), g.read()
        self.assertRegex(sig, r"^[0-9a-f]{128}\n$")
        self.assertTrue(signing.verify_bytes(data, sig, [TEST_PUB]))
        self.assert_nothing_uploaded()

    def test_no_key_refused_and_nothing_uploaded(self):
        with self.key_env(os.path.join(self.tmp.name, "absent.key")):
            self.assertEqual(publish.main(["--exe", self.exe, "--out", self.out]), 1)
        self.assert_nothing_uploaded()
        self.assertFalse(os.path.exists(self.out))

    def test_no_key_on_dry_run_warns_and_writes_no_sig(self):
        with open(self.out + ".sig", "w") as f:
            f.write("stale")
        with self.key_env(os.path.join(self.tmp.name, "absent.key")):
            self.assertEqual(publish.main(["--exe", self.exe, "--out", self.out, "--dry-run"]), 0)
        self.assertTrue(os.path.exists(self.out))
        self.assertFalse(os.path.exists(self.out + ".sig"))   # the stale one is gone, not left beside it

    def test_untrusted_key_refused_even_on_dry_run(self):
        other = write_key(os.path.join(self.tmp.name, "other.key"), OTHER_SEED)
        with self.key_env(other):
            self.assertEqual(publish.main(["--exe", self.exe, "--out", self.out, "--dry-run"]), 1)
            self.assertEqual(publish.main(["--exe", self.exe, "--out", self.out]), 1)
        self.assert_nothing_uploaded()

    def test_bad_key_files_refused(self):
        short = write_key(os.path.join(self.tmp.name, "short.key"), TEST_SEED[:31])
        hexed = write_key(os.path.join(self.tmp.name, "hex.key"), TEST_SEED.hex().encode())
        for path in (short, hexed):
            with self.subTest(path=path), self.assertRaises(signing.SigningError):
                signing.load_private_key(path)
        if os.name != "nt":
            loose = write_key(os.path.join(self.tmp.name, "loose.key"), TEST_SEED, mode=0o644)
            with self.assertRaises(signing.SigningError):
                signing.load_private_key(loose)

    def test_key_path_env_override(self):
        with self.key_env("/somewhere/k"):
            self.assertEqual(signing.key_path(), "/somewhere/k")
        with mock.patch.dict(os.environ, {}, clear=True):
            self.assertTrue(signing.key_path().replace("\\", "/").endswith("/.config/coopmods/signing.key"))

    def test_sig_not_routed_refused_before_upload(self):
        publish.fetch.side_effect = lambda url, timeout=20: (
            publish.PLACEHOLDER if url == publish.SIG_URL else self.server.fetch(url, timeout))
        self.assertEqual(publish.main(["--exe", self.exe, "--out", self.out]), 1)
        self.assert_nothing_uploaded()

    def test_signing_cli_sign_and_verify(self):
        f = os.path.join(self.tmp.name, "manifest.json")
        with open(f, "wb") as fh:
            fh.write(b'{"version": "76.0.0", "files": []}\n')
        self.assertEqual(signing.main(["sign", f]), 0)
        self.assertEqual(signing.main(["verify", f]), 0)
        with open(f, "ab") as fh:
            fh.write(b" ")
        self.assertEqual(signing.main(["verify", f]), 1)
        os.remove(f + ".sig")
        self.assertEqual(signing.main(["verify", f]), 1)


if __name__ == "__main__":
    unittest.main()
