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



# ---- the dev channel (#26) --------------------------------------------------------------------------

# Written out from docs/SPEC.md "Dev channel" and site/README.md, not read from tools/publish.py.
DEV_REMOTE = "/opt/downloads/ffb-coop-site/dev"
DEV_GAMES_URL = "https://coopmods.com/dev/games.json"
DEV_LAUNCHER_URL = "https://coopmods.com/dev/launcher/FFB%20Co-op%20-%20dev.exe"
DEV_MANIFEST_URL = "https://coopmods.com/dev/mewgenics/manifest.json"


class DevServer(FakeServer):
    """FakeServer plus the two reads the dev publish does over ssh (`cat`, `test -f`), and https that answers
    401 to anything under /dev/ without a login, as the gate in site/README.md does. `gate` False is the gate
    not deployed: the placeholder answers instead."""

    def __init__(self):
        super().__init__()
        self.gate = True
        self.https = []
        # The reads (cat, test -f) fail as ssh does when it cannot connect: exit 255. Later commands still
        # work, as when only the first connection drops.
        self.ssh_down = False

    def run(self, cmd, check=True):
        shell = cmd[-1]
        if cmd[0] == "ssh" and self.ssh_down and shell.startswith(("cat ", "test -f ")):
            self.cmds.append(cmd)
            if check:
                raise publish.Refused("fake ssh failed")
            return mock.Mock(returncode=255, stdout="", stderr="ssh: connect to host: Connection timed out")
        if cmd[0] == "ssh" and shell.startswith("cat "):
            self.cmds.append(cmd)
            data = self.files.get(shell[len("cat "):].strip("'"))
            return mock.Mock(returncode=0 if data is not None else 1, stdout=(data or b"").decode(), stderr="")
        if cmd[0] == "ssh" and shell.startswith("test -f "):
            self.cmds.append(cmd)
            return mock.Mock(returncode=0 if shell[len("test -f "):].strip("'") in self.files else 1,
                             stdout="", stderr="")
        return super().run(cmd, check)

    def http_status(self, url, login=None, timeout=20):
        self.https.append(url)
        if url.startswith("https://coopmods.com/dev/") and not login:
            return (401, b"") if self.gate else (200, publish.PLACEHOLDER)
        raise AssertionError(f"unexpected https request {url} login={login!r}")


PUBLIC_FILES = {"/opt/downloads/ffb-coop-site/games.json": b'{"public": true}\n',
                "/opt/downloads/ffb-coop-site/games.json.sig": b"public sig\n",
                "/opt/downloads/ffb-coop-site/launcher/FFB Co-op.exe": b"public exe"}


class Dev(unittest.TestCase):
    """`publish.py --dev` against DevServer: what goes where, and what it refuses."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.exe = os.path.join(self.tmp.name, "FFB Co-op - dev.exe")
        self.data = fake_exe((1, 0, 1, 0))
        with open(self.exe, "wb") as f:
            f.write(self.data)
        self.pkg = os.path.join(self.tmp.name, "pkg")
        os.makedirs(self.pkg)
        self.pkg_files = {"mewcoop_loader.exe": b"loader", "mewcoop.dll": b"dev dll", "mewcoop_ui.swf": b"swf"}
        for name, data in self.pkg_files.items():
            with open(os.path.join(self.pkg, name), "wb") as f:
                f.write(data)
        self.out = os.path.join(self.tmp.name, "dev-out")
        self.server = DevServer()
        self.server.files.update(PUBLIC_FILES)
        self.patches = [mock.patch.object(publish, "run_gates"),
                        mock.patch.object(publish, "run", side_effect=self.server.run),
                        mock.patch.object(publish, "fetch", side_effect=AssertionError("dev publish used the public fetch")),
                        mock.patch.object(publish, "http_status", side_effect=self.server.http_status),
                        mock.patch.dict(os.environ, {}, clear=False)]
        for p in self.patches: p.start()
        os.environ.pop(publish.DEV_LOGIN_ENV, None)

    def tearDown(self):
        for p in self.patches: p.stop()
        self.tmp.cleanup()

    def main(self, *extra, exe=True, package=True):
        argv = ["--dev", "--out", self.out]
        if exe:
            argv += ["--exe", self.exe]
        if package:
            argv += ["--package", "mewgenics", self.pkg, "--package-version", "77.0.1", "--optional", "mewcoop_ui.swf"]
        return publish.main(argv + list(extra))

    def live_dev(self, version):
        launcher = {"version": version, "url": DEV_LAUNCHER_URL, "sha256": "0" * 64, "size": 1}
        doc = publish.build_games_json(load(publish.DEV_GAMES_IN), launcher=launcher)
        self.server.files[DEV_REMOTE + "/games.json"] = publish.dumps(doc).encode()
        return doc

    def test_constants_are_the_spec_urls(self):
        self.assertEqual(publish.DEV_REMOTE, DEV_REMOTE)
        self.assertEqual(publish.DEV_GAMES_URL, DEV_GAMES_URL)
        self.assertEqual(publish.DEV_LAUNCHER_URL, DEV_LAUNCHER_URL)
        self.assertEqual(publish.dev_manifest_url("mewgenics"), DEV_MANIFEST_URL)

    def test_checked_in_dev_games_in_is_valid_and_reads_the_dev_manifest(self):
        launcher = {"version": "1.0.1", "url": DEV_LAUNCHER_URL, "sha256": "0" * 64, "size": 1}
        doc = publish.build_games_json(load(publish.DEV_GAMES_IN), launcher=launcher)
        self.assertEqual([g["manifest"] for g in doc["games"]], [DEV_MANIFEST_URL])

    def test_full_publish_writes_only_under_dev(self):
        self.assertEqual(self.main(), 0)
        s = self.server
        for path in s.files:
            if path not in PUBLIC_FILES:
                self.assertTrue(path.startswith(DEV_REMOTE + "/"), path)
        for p, data in PUBLIC_FILES.items():
            self.assertEqual(s.files[p], data)   # no public file changed
        for c in s.cmds:
            if c[0] == "scp":
                self.assertTrue(c[-1].split(":", 1)[1].startswith(DEV_REMOTE + "/.upload-"), c)
            elif c[-1].startswith("mv -f "):
                src, dst = c[-1][len("mv -f "):].split(" ", 1)
                self.assertTrue(src.startswith(DEV_REMOTE + "/.upload-"), c)
                self.assertTrue(dst.startswith("'" + DEV_REMOTE + "/"), c)
            elif c[-1].startswith("mkdir -p "):
                for d in c[-1][len("mkdir -p "):].split("' '"):
                    self.assertTrue(d.strip("'").startswith(DEV_REMOTE + "/"), c)
        self.assertEqual([p for p in s.files if ".upload-" in p], [])

    def test_full_publish_files_signatures_and_order(self):
        self.assertEqual(self.main(), 0)
        f = self.server.files
        self.assertEqual(f[DEV_REMOTE + "/launcher/FFB Co-op - dev.exe"], self.data)
        games = json.loads(f[DEV_REMOTE + "/games.json"])
        self.assertEqual(games["launcher"], {"version": "1.0.1", "url": DEV_LAUNCHER_URL,
                                             "sha256": hashlib.sha256(self.data).hexdigest(), "size": len(self.data)})
        self.assertTrue(signing.verify_bytes(f[DEV_REMOTE + "/games.json"], f[DEV_REMOTE + "/games.json.sig"], [TEST_PUB]))
        manifest = json.loads(f[DEV_REMOTE + "/mewgenics/manifest.json"])
        self.assertTrue(signing.verify_bytes(f[DEV_REMOTE + "/mewgenics/manifest.json"],
                                             f[DEV_REMOTE + "/mewgenics/manifest.json.sig"], [TEST_PUB]))
        self.assertEqual(manifest["version"], "77.0.1")
        self.assertEqual({e["name"]: (e["size"], e["sha256"], e["required"]) for e in manifest["files"]},
                         {n: (len(d), hashlib.sha256(d).hexdigest(), n != "mewcoop_ui.swf")
                          for n, d in self.pkg_files.items()})
        for name, data in self.pkg_files.items():
            self.assertEqual(f[DEV_REMOTE + "/mewgenics/" + name], data)
        s = self.server
        mv = lambda tail: s.index(lambda c: c[-1].startswith("mv -f ") and c[-1].endswith(tail + "'"))
        self.assertLess(mv("/launcher/FFB Co-op - dev.exe"), mv("/dev/games.json"))
        self.assertLess(mv("/mewgenics/mewcoop.dll"), mv("/mewgenics/manifest.json"))
        self.assertLess(mv("/mewgenics/manifest.json.sig"), mv("/mewgenics/manifest.json"))
        self.assertLess(mv("/mewgenics/manifest.json"), mv("/dev/games.json"))
        self.assertLess(mv("/dev/games.json.sig"), mv("/dev/games.json"))
        # the gate was checked before the first upload, and every published URL after
        first_scp = s.index(lambda c: c[0] == "scp")
        self.assertTrue(first_scp > 0 and s.https[0] == DEV_GAMES_URL)
        self.assertIn(DEV_MANIFEST_URL, s.https)
        self.assertIn(DEV_LAUNCHER_URL, s.https)

    def test_gate_not_up_refused_before_any_upload(self):
        self.server.gate = False
        self.assertEqual(self.main(), 1)
        self.assertFalse([c for c in self.server.cmds if c[0] == "scp" or c[-1].startswith(("mv ", "mkdir "))])

    def test_release_exe_refused(self):
        with open(self.exe, "wb") as f:
            f.write(fake_exe((1, 0, 0, 0)))
        self.assertEqual(self.main(), 1)
        self.assertFalse([c for c in self.server.cmds if c[0] == "scp"])

    def test_dev_exe_refused_by_the_public_publish(self):
        out = os.path.join(self.tmp.name, "games.json")
        self.assertEqual(publish.main(["--exe", self.exe, "--out", out, "--dry-run"]), 1)

    def test_release_flag_with_dev_refused(self):
        self.assertEqual(self.main("--release", "1"), 1)
        self.assertEqual(self.server.cmds, [])

    def test_lower_or_same_version_different_bytes_than_live_dev_refused(self):
        self.live_dev("1.0.2")
        self.assertEqual(self.main(), 1)
        self.live_dev("1.0.1")   # same version, other bytes
        self.assertEqual(self.main(), 1)
        self.assertFalse([c for c in self.server.cmds if c[0] == "scp"])

    def test_ssh_down_is_not_an_empty_server(self):
        # A live 1.0.1 with other bytes would refuse this exe; an ssh that cannot connect must not hide it.
        self.live_dev("1.0.1")
        self.server.ssh_down = True
        self.assertEqual(self.main(), 1)
        self.assertFalse([c for c in self.server.cmds if c[0] == "scp"])
        with self.assertRaises(publish.Refused):
            publish.remote_exists(DEV_REMOTE + "/mewgenics/manifest.json")

    def test_missing_file_is_none_and_present_file_is_read(self):
        self.assertIsNone(publish.fetch_live_dev_games())
        self.assertFalse(publish.remote_exists(DEV_REMOTE + "/mewgenics/manifest.json"))
        live = self.live_dev("1.0.3")
        self.assertEqual(publish.fetch_live_dev_games(), live)

    def test_newer_than_live_dev_accepted(self):
        self.live_dev("1.0.0")
        self.assertEqual(self.main(), 0)

    def test_dev_manifest_not_live_and_not_published_refused(self):
        self.assertEqual(self.main(package=False), 1)
        self.assertFalse([c for c in self.server.cmds if c[0] == "scp"])
        self.server.files[DEV_REMOTE + "/mewgenics/manifest.json"] = b"{}"   # published earlier
        self.assertEqual(self.main(package=False), 0)

    def test_no_exe_keeps_the_live_launcher(self):
        live = self.live_dev("1.0.3")
        self.assertEqual(self.main("--no-exe", exe=False), 0)
        f = self.server.files
        self.assertNotIn(DEV_REMOTE + "/launcher/FFB Co-op - dev.exe", f)
        self.assertEqual(json.loads(f[DEV_REMOTE + "/games.json"])["launcher"], live["launcher"])

    def test_no_exe_without_live_refused(self):
        self.assertEqual(self.main("--no-exe", exe=False), 1)

    def test_package_for_a_public_manifest_refused(self):
        games_in = os.path.join(self.tmp.name, "dev-games.json.in")
        with open(games_in, "w") as f:
            json.dump(GAMES_IN, f)   # mewgenics reads the public manifest
        self.assertEqual(self.main("--games-in", games_in), 1)

    def test_package_without_its_launcher_refused(self):
        os.remove(os.path.join(self.pkg, "mewcoop_loader.exe"))
        self.assertEqual(self.main(), 1)
        self.assertFalse([c for c in self.server.cmds if c[0] == "scp"])

    def test_package_with_its_launcher_optional_refused(self):
        self.assertEqual(self.main("--optional", "mewcoop_loader.exe"), 1)
        self.assertFalse([c for c in self.server.cmds if c[0] == "scp"])

    def test_bad_package_folders_refused(self):
        with self.assertRaises(publish.Refused):
            publish.build_package_manifest(os.path.join(self.tmp.name, "nope"), "77.0.1")
        empty = os.path.join(self.tmp.name, "empty")
        os.makedirs(empty)
        with self.assertRaises(publish.Refused):
            publish.build_package_manifest(empty, "77.0.1")
        with self.assertRaises(publish.Refused):
            publish.build_package_manifest(self.pkg, "seventy")
        with self.assertRaises(publish.Refused):
            publish.build_package_manifest(self.pkg, "77.0.1", optional=("absent.dll",))
        for bad in ("CON.dll", "manifest.json", "x.sig"):
            d = os.path.join(self.tmp.name, "bad-" + bad)
            os.makedirs(d)
            with open(os.path.join(d, bad), "wb") as f:
                f.write(b"x")
            with self.subTest(name=bad), self.assertRaises(publish.Refused):
                publish.build_package_manifest(d, "77.0.1")

    def test_dev_put_never_leaves_the_dev_folder(self):
        for name in ("../games.json", "/games.json", "..", "a/b/c", "launcher/../../games.json", "",
                     "launcher/", "x/CON"):
            with self.subTest(name=name), self.assertRaises(publish.Refused):
                publish.dev_put(self.exe, name, "0" * 64)
        self.assertEqual(self.server.cmds, [])

    def test_public_games_json_pointing_at_dev_refused(self):
        doc = publish.build_games_json(copy.deepcopy(GAMES_IN), fake_exe((1, 0, 0, 0)))
        publish.check_no_dev_urls(doc)   # the real list passes
        doc["games"][0]["manifest"] = DEV_MANIFEST_URL
        with self.assertRaises(publish.Refused):
            publish.check_no_dev_urls(doc)

    def test_dev_flags_without_dev_refused(self):
        for extra in (["--no-exe"], ["--package", "mewgenics", self.pkg], ["--package-version", "1"],
                      ["--optional", "x"]):
            with self.subTest(extra=extra):
                self.assertEqual(publish.main(["--exe", self.exe, "--dry-run"] + extra), 1)

    def test_dry_run_writes_and_uploads_nothing(self):
        self.server.run = mock.Mock(side_effect=AssertionError("dry run touched the server"))
        publish.run.side_effect = self.server.run
        with mock.patch.object(publish, "fetch_live_dev_games", return_value=None):
            self.assertEqual(self.main("--dry-run"), 0)
        self.assertTrue(os.path.exists(os.path.join(self.out, "games.json")))
        self.assertTrue(os.path.exists(os.path.join(self.out, "mewgenics", "manifest.json.sig")))


if __name__ == "__main__":
    unittest.main()
