"""Unit tests for tools/publish.py: the games.json writer and its refusals. No network, no exe needed.

    python -m unittest discover -s tests -p "test_*.py"
"""
import copy, hashlib, json, os, struct, sys, tempfile, unittest
from unittest import mock

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tools"))
import publish  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


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


if __name__ == "__main__":
    unittest.main()
