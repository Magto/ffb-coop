// test_package.cpp -- the package update (#4, docs/SPEC.md "Package download")
// against a fake server in memory and a real temporary folder. No network.
//
// Fixture hashes come from ffb::sha256_hex, which test_sha256 checks against the
// FIPS vectors; the file contents themselves are made up here.
#include "ffb_test.h"
#include "net.h"
#include "package.h"

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

const std::string kManifestUrl = "https://example.test/update/manifest.json";
const std::string kBase        = "https://example.test/update/";

// Serves whatever the test put in `pages`; anything else is a 404. Records every
// URL asked for, so a test can see what was (not) downloaded.
struct FakeNet : ffb::Net {
    struct Page { int status; std::string body; };
    std::map<std::string, Page> pages;
    std::vector<std::string>    asked;

    int get(const std::string& url, const ffb::Sink& sink) override {
        asked.push_back(url);
        auto it = pages.find(url);
        if (it == pages.end()) return 404;
        if (it->second.status == 200) {
            // In two pieces, the way a real transfer arrives.
            const std::string& b = it->second.body;
            const std::size_t half = b.size() / 2;
            if (sink(b.data(), half)) sink(b.data() + half, b.size() - half);
        }
        return it->second.status;
    }
    bool was_asked(const std::string& url) const {
        for (const auto& a : asked) if (a == url) return true;
        return false;
    }
    std::size_t files_asked() const {   // everything but the manifest
        std::size_t n = 0;
        for (const auto& a : asked) if (a != kManifestUrl) ++n;
        return n;
    }
};

struct F { std::string name, content; bool required; };

std::string file_json(const std::string& name, std::uint64_t size, const std::string& sha, bool required) {
    return "{\"name\":\"" + name + "\",\"size\":" + std::to_string(size) + ",\"sha256\":\"" + sha +
           "\",\"required\":" + (required ? "true" : "false") + "}";
}

std::string manifest_for(const std::vector<F>& files) {
    std::string s = "{\"version\":\"76.0.0\",\"wire\":34,\"files\":[";
    for (std::size_t i = 0; i < files.size(); ++i) {
        if (i) s += ",";
        s += file_json(files[i].name, files[i].content.size(), ffb::sha256_hex(files[i].content), files[i].required);
    }
    return s + "]}";
}

// A server that serves `files` correctly.
FakeNet serving(const std::vector<F>& files) {
    FakeNet net;
    net.pages[kManifestUrl] = {200, manifest_for(files)};
    for (const auto& f : files) net.pages[kBase + f.name] = {200, f.content};
    return net;
}

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void write_file(const fs::path& p, const std::string& s) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << s;
}

bool no_staged_files(const fs::path& dir) {
    for (const auto& e : fs::directory_iterator(dir))
        if (e.path().extension() == ".new") return false;
    return true;
}

// A fresh, empty temporary folder per case: <tmp>/ffb_test_package_<pid-ish>/<name>/FFB Co-op
struct Sandbox {
    fs::path root, pkg;
    explicit Sandbox(const std::string& name) {
        root = fs::temp_directory_path() / ("ffb_test_package_" + std::to_string(std::rand())) / name;
        fs::remove_all(root);
        fs::create_directories(root);
        pkg = root / "FFB Co-op";
    }
    ~Sandbox() { std::error_code ec; fs::remove_all(root.parent_path(), ec); }
    std::string dir() const { return pkg.u8string(); }
};

const F kLoader{"mewcoop_loader.exe", "loader v76 bytes", true};
const F kDll{"mewcoop.dll", std::string(3000, 'd') + "dll v76", true};
const F kSwf{"mewcoop_ui.swf", std::string(5000, 's') + "swf v76", false};

bool parses(const std::string& text, std::string* why = nullptr) {
    ffb::Manifest m;
    std::string w;
    const bool ok = ffb::parse_manifest(text, &m, &w);
    if (why) *why = w;
    return ok;
}

const std::string kSha = std::string(64, 'a');

}  // namespace

int main() {
    std::srand((unsigned)std::time(nullptr));

    std::puts("manifest: a valid one parses, required defaults to true, unknown keys ignored");
    {
        ffb::Manifest m;
        std::string why;
        const std::string text = "{\"version\":\"76.0.0\",\"wire\":34,\"extra\":1,\"files\":["
                                 "{\"name\":\"a.dll\",\"size\":5,\"sha256\":\"" + kSha + "\",\"x\":0},"
                                 "{\"name\":\"b.swf\",\"size\":7,\"sha256\":\"" + kSha + "\",\"required\":false}]}";
        CHECK(ffb::parse_manifest(text, &m, &why));
        CHECK(m.version == "76.0.0");
        CHECK(m.files.size() == 2);
        CHECK(m.files.size() == 2 && m.files[0].required && !m.files[1].required);
        CHECK(m.files.size() == 2 && m.files[0].size == 5);
        CHECK(parses("{\"version\":\"76\",\"files\":[" + file_json("a.dll", 1, kSha, true) + "]}"));
    }

    std::puts("manifest: every rule rejects the whole file");
    {
        const std::string ok_file = file_json("a.dll", 1, kSha, true);
        CHECK(!parses("not json"));
        CHECK(!parses("[1,2]"));
        CHECK(!parses("{\"files\":[" + ok_file + "]}"));                        // no version
        CHECK(!parses("{\"version\":76,\"files\":[" + ok_file + "]}"));          // version not a string
        CHECK(!parses("{\"version\":\"1.2\",\"files\":[" + ok_file + "]}"));     // neither N nor X.Y.Z
        CHECK(!parses("{\"version\":\"1.2.x\",\"files\":[" + ok_file + "]}"));
        CHECK(!parses("{\"version\":\"1\"}"));                                   // no files
        CHECK(!parses("{\"version\":\"1\",\"files\":[]}"));                      // empty files
        CHECK(!parses("{\"version\":\"1\",\"files\":[5]}"));
        CHECK(!parses("{\"version\":\"1\",\"files\":[{\"size\":1,\"sha256\":\"" + kSha + "\"}]}"));  // no name
        CHECK(!parses("{\"version\":\"1\",\"files\":[" + ok_file + "," + file_json("A.DLL", 1, kSha, true) + "]}"));
        CHECK(!parses("{\"version\":\"1\",\"files\":[{\"name\":\"a.dll\",\"sha256\":\"" + kSha + "\"}]}"));  // no size
        CHECK(!parses("{\"version\":\"1\",\"files\":[" + file_json("a.dll", 0, kSha, true) + "]}"));
        CHECK(!parses("{\"version\":\"1\",\"files\":[{\"name\":\"a.dll\",\"size\":-3,\"sha256\":\"" + kSha + "\"}]}"));
        CHECK(!parses("{\"version\":\"1\",\"files\":[{\"name\":\"a.dll\",\"size\":1.5,\"sha256\":\"" + kSha + "\"}]}"));
        CHECK(!parses("{\"version\":\"1\",\"files\":[{\"name\":\"a.dll\",\"size\":\"1\",\"sha256\":\"" + kSha + "\"}]}"));
        CHECK(!parses("{\"version\":\"1\",\"files\":[{\"name\":\"a.dll\",\"size\":1}]}"));  // no sha256
        CHECK(!parses("{\"version\":\"1\",\"files\":[" + file_json("a.dll", 1, kSha.substr(1), true) + "]}"));
        CHECK(!parses("{\"version\":\"1\",\"files\":[" + file_json("a.dll", 1, "z" + kSha.substr(1), true) + "]}"));
        CHECK(!parses("{\"version\":\"1\",\"files\":[{\"name\":\"a.dll\",\"size\":1,\"sha256\":\"" + kSha +
                      "\",\"required\":\"yes\"}]}"));
    }

    std::puts("manifest: a network file name with a path in it is refused");
    {
        for (const char* bad : {"../evil.dll", "..\\\\evil.dll", "sub/evil.dll", "sub\\\\evil.dll",
                                "C:evil.dll", "C:/evil.dll", "..", ".", "a..b.dll", "CON", "nul.txt"}) {
            std::string why;
            const bool ok = parses("{\"version\":\"1\",\"files\":[" + file_json(bad, 1, kSha, true) + "]}", &why);
            CHECK(!ok);
            CHECK(why.find("rejected file name") != std::string::npos);
        }
    }

    std::puts("sibling_url: the manifest's last path segment becomes the file name");
    {
        CHECK(ffb::sibling_url("https://mewgenics.coopmods.com/update/manifest.json", "mewcoop.dll") ==
              "https://mewgenics.coopmods.com/update/mewcoop.dll");
        CHECK(ffb::sibling_url("https://h/update/manifest.json?x=1#y", "a.dll") == "https://h/update/a.dll");
        CHECK(ffb::sibling_url("https://h", "a.dll") == "https://h/a.dll");
        CHECK(ffb::sibling_url("https://h/m.json", "My File#1.dll") == "https://h/My%20File%231.dll");
    }

    std::puts("fresh install: required and optional files are all fetched (\"Always fetch\")");
    {
        Sandbox sb("fresh");
        FakeNet net = serving({kLoader, kDll, kSwf});
        const ffb::PackageResult r = ffb::update_package(sb.dir(), kManifestUrl, "mewcoop_loader.exe", net, nullptr);
        CHECK(r.outcome == ffb::PackageOutcome::Current);
        CHECK(r.version == "76.0.0");
        CHECK(r.updated.size() == 3);
        CHECK(r.warnings.empty());
        CHECK(read_file(sb.pkg / kLoader.name) == kLoader.content);
        CHECK(read_file(sb.pkg / kDll.name) == kDll.content);
        CHECK(read_file(sb.pkg / kSwf.name) == kSwf.content);
        CHECK(no_staged_files(sb.pkg));

        std::puts("already up to date: a second run downloads nothing");
        FakeNet again = serving({kLoader, kDll, kSwf});
        const ffb::PackageResult r2 = ffb::update_package(sb.dir(), kManifestUrl, "mewcoop_loader.exe", again, nullptr);
        CHECK(r2.outcome == ffb::PackageOutcome::Current);
        CHECK(r2.updated.empty());
        CHECK(again.files_asked() == 0);
    }

    std::puts("update: only the changed file is downloaded; an upper-case sha256 still matches");
    {
        Sandbox sb("changed");
        fs::create_directories(sb.pkg);
        write_file(sb.pkg / kLoader.name, kLoader.content);
        write_file(sb.pkg / kSwf.name, kSwf.content);
        write_file(sb.pkg / kDll.name, "dll v75");
        FakeNet net = serving({kLoader, kDll, kSwf});
        std::string& text = net.pages[kManifestUrl].body;
        const std::string lower = ffb::sha256_hex(kLoader.content);
        std::string upper = lower;
        for (char& c : upper) if (c >= 'a' && c <= 'f') c = (char)(c - 'a' + 'A');
        text.replace(text.find(lower), lower.size(), upper);
        const ffb::PackageResult r = ffb::update_package(sb.dir(), kManifestUrl, "", net, nullptr);
        CHECK(r.outcome == ffb::PackageOutcome::Current);
        CHECK(r.updated.size() == 1 && r.updated[0] == kDll.name);
        CHECK(net.files_asked() == 1);
        CHECK(net.was_asked(kBase + kDll.name));
        CHECK(read_file(sb.pkg / kDll.name) == kDll.content);
    }

    std::puts("bad sha256 on a required file: refused, and nothing is replaced");
    {
        Sandbox sb("badsha");
        fs::create_directories(sb.pkg);
        write_file(sb.pkg / kLoader.name, "loader v75");
        write_file(sb.pkg / kDll.name, "dll v75");
        FakeNet net = serving({kLoader, kDll});
        // Same size, different bytes: only the hash can tell.
        std::string forged = kDll.content;
        forged[0] = 'X';
        net.pages[kBase + kDll.name].body = forged;
        const ffb::PackageResult r = ffb::update_package(sb.dir(), kManifestUrl, "", net, nullptr);
        CHECK(r.outcome == ffb::PackageOutcome::Failed);
        CHECK(r.reason.find(kDll.name) != std::string::npos);
        CHECK(r.reason.find("sha256") != std::string::npos);
        CHECK(r.updated.empty());
        CHECK(read_file(sb.pkg / kDll.name) == "dll v75");
        CHECK(read_file(sb.pkg / kLoader.name) == "loader v75");   // fetched fine, still not replaced
        CHECK(no_staged_files(sb.pkg));
    }

    std::puts("wrong size on a required file: refused, the old file stays");
    {
        for (const std::string& body : {kDll.content + "more", kDll.content.substr(1)}) {
            Sandbox sb("badsize");
            fs::create_directories(sb.pkg);
            write_file(sb.pkg / kDll.name, "dll v75");
            FakeNet net = serving({kDll});
            net.pages[kBase + kDll.name].body = body;
            const ffb::PackageResult r = ffb::update_package(sb.dir(), kManifestUrl, "", net, nullptr);
            CHECK(r.outcome == ffb::PackageOutcome::Failed);
            CHECK(read_file(sb.pkg / kDll.name) == "dll v75");
            CHECK(no_staged_files(sb.pkg));
        }
    }

    std::puts("a path in a file name: the manifest is refused and nothing is written anywhere");
    {
        for (const char* bad : {"../evil.dll", "..\\\\evil.dll", "sub/evil.dll", "C:evil.dll"}) {
            Sandbox sb("path");
            FakeNet net;
            net.pages[kManifestUrl] = {200, "{\"version\":\"1\",\"files\":[" +
                                                file_json(kDll.name, kDll.content.size(), ffb::sha256_hex(kDll.content), true) + "," +
                                                file_json(bad, 4, ffb::sha256_hex("evil"), true) + "]}"};
            net.pages[kBase + kDll.name] = {200, kDll.content};
            const ffb::PackageResult r = ffb::update_package(sb.dir(), kManifestUrl, "", net, nullptr);
            CHECK(r.outcome == ffb::PackageOutcome::Invalid);
            CHECK(net.files_asked() == 0);
            CHECK(!fs::exists(sb.pkg));             // not even the package folder
            CHECK(!fs::exists(sb.root / "evil.dll"));
        }
    }

    std::puts("optional file fails on a fresh install: a warning, the required files still install");
    {
        Sandbox sb("optmissing");
        FakeNet net = serving({kLoader, kDll, kSwf});
        net.pages.erase(kBase + kSwf.name);   // 404
        const ffb::PackageResult r = ffb::update_package(sb.dir(), kManifestUrl, "mewcoop_loader.exe", net, nullptr);
        CHECK(r.outcome == ffb::PackageOutcome::Current);
        CHECK(r.warnings.size() == 1 && r.warnings[0].find(kSwf.name) != std::string::npos);
        CHECK(read_file(sb.pkg / kLoader.name) == kLoader.content);
        CHECK(read_file(sb.pkg / kDll.name) == kDll.content);
        CHECK(!fs::exists(sb.pkg / kSwf.name));
        CHECK(no_staged_files(sb.pkg));
    }

    std::puts("optional file with a bad sha256: a warning, the old optional file stays");
    {
        Sandbox sb("optbadsha");
        fs::create_directories(sb.pkg);
        write_file(sb.pkg / kSwf.name, "swf v75");
        FakeNet net = serving({kDll, kSwf});
        std::string forged = kSwf.content;
        forged[0] = 'X';
        net.pages[kBase + kSwf.name].body = forged;
        const ffb::PackageResult r = ffb::update_package(sb.dir(), kManifestUrl, "", net, nullptr);
        CHECK(r.outcome == ffb::PackageOutcome::Current);
        CHECK(r.warnings.size() == 1);
        CHECK(read_file(sb.pkg / kSwf.name) == "swf v75");
        CHECK(read_file(sb.pkg / kDll.name) == kDll.content);
    }

    std::puts("required file missing on the server: the update fails, nothing is replaced");
    {
        Sandbox sb("reqmissing");
        FakeNet net = serving({kLoader, kDll, kSwf});
        net.pages.erase(kBase + kDll.name);
        const ffb::PackageResult r = ffb::update_package(sb.dir(), kManifestUrl, "", net, nullptr);
        CHECK(r.outcome == ffb::PackageOutcome::Failed);
        CHECK(r.reason.find(kDll.name) != std::string::npos);
        CHECK(!fs::exists(sb.pkg / kLoader.name));
        CHECK(!fs::exists(sb.pkg / kSwf.name));
        CHECK(no_staged_files(sb.pkg));
    }

    std::puts("a required file that cannot be replaced: the old one stays and the reason names it");
    {
        Sandbox sb("noreplace");
        // A non-empty folder where the file goes: no rename can put a file over it,
        // on Windows or POSIX -- the stand-in for a file the game holds open.
        fs::create_directories(sb.pkg / kDll.name / "held");
        FakeNet net = serving({kLoader, kDll});
        const ffb::PackageResult r = ffb::update_package(sb.dir(), kManifestUrl, "", net, nullptr);
        CHECK(r.outcome == ffb::PackageOutcome::Failed);
        CHECK(r.reason.find("could not replace " + kDll.name) != std::string::npos);
        CHECK(fs::is_directory(sb.pkg / kDll.name / "held"));
        CHECK(read_file(sb.pkg / kLoader.name) == kLoader.content);
        CHECK(no_staged_files(sb.pkg));
    }

    std::puts("the manifest: unreachable, an HTTP error, invalid, not https, launcher not required");
    {
        Sandbox sb("manifest");
        FakeNet none;   // every URL a 404; a 0 needs its own page
        none.pages[kManifestUrl] = {0, ""};
        CHECK(ffb::update_package(sb.dir(), kManifestUrl, "", none, nullptr).outcome == ffb::PackageOutcome::Unreachable);
        FakeNet missing;
        CHECK(ffb::update_package(sb.dir(), kManifestUrl, "", missing, nullptr).outcome == ffb::PackageOutcome::Unreachable);
        FakeNet garbage;
        garbage.pages[kManifestUrl] = {200, "<html>"};
        CHECK(ffb::update_package(sb.dir(), kManifestUrl, "", garbage, nullptr).outcome == ffb::PackageOutcome::Invalid);
        FakeNet plain_http = serving({kDll});
        CHECK(ffb::update_package(sb.dir(), "http://example.test/update/manifest.json", "", plain_http, nullptr).outcome ==
              ffb::PackageOutcome::Invalid);
        CHECK(plain_http.asked.empty());
        FakeNet no_launcher = serving({kDll});
        CHECK(ffb::update_package(sb.dir(), kManifestUrl, "mewcoop_loader.exe", no_launcher, nullptr).outcome ==
              ffb::PackageOutcome::Invalid);
        FakeNet optional_launcher = serving({{"mewcoop_loader.exe", "x", false}});
        CHECK(ffb::update_package(sb.dir(), kManifestUrl, "mewcoop_loader.exe", optional_launcher, nullptr).outcome ==
              ffb::PackageOutcome::Invalid);
        CHECK(no_launcher.files_asked() == 0 && optional_launcher.files_asked() == 0);
    }

    return ffb_test_result();
}
