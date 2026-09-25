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
        self.assertEqual(len(scps), 2)
        for c in scps:
            self.assertTrue(c[-1].split(":", 1)[1].startswith(publish.REMOTE + "/.upload-"), c)
        exe_mv = s.index(lambda c: c[-1].startswith("mv -f ") and c[-1].endswith(f"/launcher/{publish.EXE_NAME}'"))
        games_scp = s.index(lambda c: c[0] == "scp" and c[-2] == self.out)
        games_mv = s.index(lambda c: c[-1].startswith("mv -f ") and c[-1].endswith("/games.json'"))
        self.assertLess(exe_mv, games_scp)
        self.assertLess(games_scp, games_mv)
        self.assertEqual(s.files[publish.REMOTE + "/launcher/" + publish.EXE_NAME], self.data)
        self.assertEqual(json.loads(s.files[publish.REMOTE + "/games.json"]), load(self.out))
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
        good = {publish.URL + "/games.json": dumps_bytes(doc), publish.LAUNCHER_URL: self.data}
        publish.fetch.side_effect = lambda url, timeout=20: good[url]
        publish.verify_live(doc)   # the right bytes pass
        for url, other in ((publish.LAUNCHER_URL, self.data + b"x"), (publish.LAUNCHER_URL, None),
                           (publish.URL + "/games.json", b'{"schema": 1}'), (publish.URL + "/games.json", None)):
            answers = dict(good, **{url: other})
            publish.fetch.side_effect = lambda u, timeout=20, a=answers: a[u]
            with self.subTest(url=url, other=other), self.assertRaises(publish.Refused):
                publish.verify_live(doc)

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


if __name__ == "__main__":
    unittest.main()
