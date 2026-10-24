// test_mirror.cpp -- ffbcoop.com first, coopmods.com second (#35, docs/SPEC.md
// "Server"): the order mirror_urls() gives each URL, which hosts have a twin,
// when MirrorNet and first_answer() (the self-update's rule) move on to the next
// name and when they must not, that a silent name is skipped for the rest of the
// start, the lines logged, and that the dev login goes to both names of /dev/.
//
// The inner Net is a fake that answers per URL; MirrorNet itself is the real
// one. The expected URLs and lines are written out from the issue's Done looks
// like, not taken from a run.
#include "channel.h"
#include "net.h"
#include "ffb_test.h"

#include <map>
#include <string>
#include <vector>

using namespace ffb;

namespace {

// Answers each URL with {status, body}; a URL it does not know is a 404. A
// status 0 with a body sends the body and then fails, as a read that times out
// halfway does.
struct FakeNet : Net {
    std::map<std::string, std::pair<int, std::string>> pages;
    std::vector<std::string> asked;
    int get(const std::string& url, const Sink& sink) override {
        asked.push_back(url);
        const auto it = pages.find(url);
        if (it == pages.end()) return 404;
        if (!it->second.second.empty() && (it->second.first == 200 || it->second.first == 0))
            sink(it->second.second.data(), it->second.second.size());
        return it->second.first;
    }
};

struct Mirror {
    FakeNet                  inner;
    std::vector<std::string> log;
    MirrorNet                net{inner, [this](const std::string& l) { log.push_back(l); }};
    int get(const std::string& url, std::string* body) { return get_body(net, url, body); }
};

const std::vector<std::string> kNone;

std::vector<std::string> v(std::initializer_list<const char*> l) {
    return std::vector<std::string>(l.begin(), l.end());
}

void test_order() {
    std::printf("a URL on either name: the ffbcoop.com form first, the coopmods.com form second\n");
    const auto games = v({"https://ffbcoop.com/games.json", "https://coopmods.com/games.json"});
    CHECK(mirror_urls("https://coopmods.com/games.json") == games);
    CHECK(mirror_urls("https://ffbcoop.com/games.json") == games);
    const auto exe = v({"https://ffbcoop.com/launcher/FFB%20Co-op.exe", "https://coopmods.com/launcher/FFB%20Co-op.exe"});
    CHECK(mirror_urls("https://coopmods.com/launcher/FFB%20Co-op.exe") == exe);
    const auto man = v({"https://mewgenics.ffbcoop.com/update/manifest.json",
                        "https://mewgenics.coopmods.com/update/manifest.json"});
    CHECK(mirror_urls("https://mewgenics.coopmods.com/update/manifest.json") == man);
    CHECK(mirror_urls("https://mewgenics.ffbcoop.com/update/manifest.json") == man);
    CHECK(mirror_urls("https://www.coopmods.com/games.json") ==
          v({"https://www.ffbcoop.com/games.json", "https://www.coopmods.com/games.json"}));
    // the host in any case; the path, query and fragment exactly as given
    CHECK(mirror_urls("https://CoopMods.COM/Dev/a%20b.json?x=1#y") ==
          v({"https://ffbcoop.com/Dev/a%20b.json?x=1#y", "https://coopmods.com/Dev/a%20b.json?x=1#y"}));
    CHECK(mirror_urls("https://coopmods.com") == v({"https://ffbcoop.com", "https://coopmods.com"}));

    std::printf("anything else: the URL alone\n");
    for (const char* u : {"https://example.test/update/manifest.json", "https://other.coopmods.com/update/manifest.json",
                          "https://coopmods.com.evil.test/games.json", "https://evil.test/https://coopmods.com/games.json",
                          "https://notcoopmods.com/games.json", "http://coopmods.com/games.json",
                          "https://coopmods.com:8443/games.json", "https://dev@coopmods.com/games.json", "", "coopmods.com"})
        CHECK(mirror_urls(u) == v({u}));
}

void test_host() {
    std::printf("url_host: lowercased, https only, no port or user\n");
    CHECK(url_host("https://Mewgenics.FFBcoop.com/update/manifest.json") == "mewgenics.ffbcoop.com");
    CHECK(url_host("https://ffbcoop.com?x") == "ffbcoop.com");
    CHECK(url_host("http://ffbcoop.com/") == "");
    CHECK(url_host("https://ffbcoop.com:443/") == "");
    CHECK(url_host("https://a@ffbcoop.com/") == "");
}

void test_ffbcoop_answers() {
    std::printf("ffbcoop.com answers: one request, to ffbcoop.com, and one line naming it\n");
    Mirror m;
    m.inner.pages["https://ffbcoop.com/games.json"] = {200, "{}"};
    std::string body;
    CHECK(m.get("https://coopmods.com/games.json", &body) == 200);
    CHECK(body == "{}");
    CHECK(m.inner.asked == v({"https://ffbcoop.com/games.json"}));
    CHECK(m.log == v({"[*] reached ffbcoop.com"}));

    std::printf("the next file from the same host logs nothing; a new host logs its name once\n");
    m.inner.pages["https://ffbcoop.com/games.json.sig"] = {200, "sig"};
    m.inner.pages["https://mewgenics.ffbcoop.com/update/manifest.json"] = {200, "m"};
    CHECK(m.get("https://ffbcoop.com/games.json.sig", &body) == 200);
    CHECK(m.get("https://mewgenics.coopmods.com/update/manifest.json", &body) == 200 && body == "m");
    CHECK(m.log == v({"[*] reached ffbcoop.com", "[*] reached mewgenics.ffbcoop.com"}));
}

void test_fallback() {
    std::printf("ffbcoop.com does not answer: the same path on coopmods.com, and the log says so\n");
    Mirror m;
    m.inner.pages["https://ffbcoop.com/games.json"]  = {0, ""};
    m.inner.pages["https://coopmods.com/games.json"] = {200, "{}"};
    std::string body;
    CHECK(m.get("https://ffbcoop.com/games.json", &body) == 200);
    CHECK(body == "{}");
    CHECK(m.inner.asked == v({"https://ffbcoop.com/games.json", "https://coopmods.com/games.json"}));
    CHECK(m.log == v({"[!] ffbcoop.com did not answer -- trying coopmods.com", "[*] reached coopmods.com"}));

    std::printf("a coopmods.com URL from games.json: its ffbcoop.com twin first, the same fallback\n");
    Mirror n;
    n.inner.pages["https://mewgenics.ffbcoop.com/update/mewcoop.dll"]  = {0, ""};
    n.inner.pages["https://mewgenics.coopmods.com/update/mewcoop.dll"] = {200, "dll"};
    CHECK(n.get("https://mewgenics.coopmods.com/update/mewcoop.dll", &body) == 200 && body == "dll");
    CHECK(n.inner.asked == v({"https://mewgenics.ffbcoop.com/update/mewcoop.dll",
                              "https://mewgenics.coopmods.com/update/mewcoop.dll"}));

    std::printf("neither name answers: 0, both tried, nothing delivered\n");
    Mirror o;
    o.inner.pages["https://ffbcoop.com/games.json"]  = {0, ""};
    o.inner.pages["https://coopmods.com/games.json"] = {0, ""};
    CHECK(o.get("https://coopmods.com/games.json", &body) == 0 && body.empty());
    CHECK(o.inner.asked.size() == 2);
    CHECK(o.log == v({"[!] ffbcoop.com did not answer -- trying coopmods.com"}));
}

void test_no_fallback() {
    std::printf("an HTTP status is ffbcoop.com's answer: never retried on coopmods.com\n");
    for (int status : {404, 401, 403, 500}) {
        Mirror m;
        m.inner.pages["https://ffbcoop.com/dev/games.json"]  = {status, ""};
        m.inner.pages["https://coopmods.com/dev/games.json"] = {200, "{}"};
        std::string body;
        CHECK(m.get("https://ffbcoop.com/dev/games.json", &body) == status);
        CHECK(body.empty());
        CHECK(m.inner.asked == v({"https://ffbcoop.com/dev/games.json"}));
    }

    std::printf("a transfer that fails after its first byte is not retried: no byte arrives twice\n");
    Mirror m;
    m.inner.pages["https://ffbcoop.com/launcher/FFB%20Co-op.exe"]  = {0, "half"};
    m.inner.pages["https://coopmods.com/launcher/FFB%20Co-op.exe"] = {200, "whole"};
    std::string body;
    CHECK(m.get("https://coopmods.com/launcher/FFB%20Co-op.exe", &body) == 0);
    CHECK(body == "half");
    CHECK(m.inner.asked.size() == 1);

    std::printf("a URL on no twin host: asked once, as given\n");
    Mirror e;
    e.inner.pages["https://example.test/update/manifest.json"] = {0, ""};
    CHECK(e.get("https://example.test/update/manifest.json", &body) == 0);
    CHECK(e.inner.asked == v({"https://example.test/update/manifest.json"}));
    CHECK(e.log == kNone);
}

void test_first_answer() {
    std::printf("first_answer, the rule the self-update runs: ffbcoop.com first, coopmods.com only without an answer\n");
    std::vector<std::string> tried, skipped;
    auto run = [&](const std::string& url, std::vector<bool> answers) {
        tried.clear();
        skipped.clear();
        return first_answer(
            url,
            [&](const std::string& u) {
                tried.push_back(u);
                return answers[tried.size() - 1];
            },
            [&](const std::string& host, const std::string& next) { skipped.push_back(host + " -> " + next); });
    };
    const std::string exe_ffb = "https://ffbcoop.com/launcher/FFB%20Co-op.exe";
    const std::string exe_old = "https://coopmods.com/launcher/FFB%20Co-op.exe";

    // ffbcoop.com answers (any status, a failed download included): nothing more is tried
    CHECK(run(exe_old, {true, true}) == exe_ffb);
    CHECK(tried == v({exe_ffb.c_str()}));
    CHECK(skipped == kNone);

    // ffbcoop.com does not answer: coopmods.com, and one skip naming both
    CHECK(run(exe_old, {false, true}) == exe_old);
    CHECK(tried == v({exe_ffb.c_str(), exe_old.c_str()}));
    CHECK(skipped == v({"ffbcoop.com -> coopmods.com"}));

    // neither answers: "", both tried, one skip (there is no third name to try)
    CHECK(run(exe_ffb, {false, false}) == "");
    CHECK(tried.size() == 2 && skipped.size() == 1);

    // no twin: asked once, as given
    CHECK(run("https://example.test/x.exe", {false}) == "");
    CHECK(tried == v({"https://example.test/x.exe"}) && skipped == kNone);
}

void test_silent_name_skipped() {
    std::printf("a name that did not answer is not asked again this start: one wait, one line\n");
    Mirror m;
    m.inner.pages["https://ffbcoop.com/games.json"]      = {0, ""};
    m.inner.pages["https://coopmods.com/games.json"]     = {200, "{}"};
    m.inner.pages["https://coopmods.com/games.json.sig"] = {200, "sig"};
    std::string body;
    CHECK(m.get("https://ffbcoop.com/games.json", &body) == 200);
    CHECK(m.get("https://ffbcoop.com/games.json.sig", &body) == 200 && body == "sig");
    CHECK(m.inner.asked == v({"https://ffbcoop.com/games.json", "https://coopmods.com/games.json",
                              "https://coopmods.com/games.json.sig"}));
    CHECK(m.log == v({"[!] ffbcoop.com did not answer -- trying coopmods.com", "[*] reached coopmods.com"}));

    std::printf("another host is its own name: mewgenics.ffbcoop.com is still asked first\n");
    m.inner.pages["https://mewgenics.ffbcoop.com/update/manifest.json"] = {200, "m"};
    CHECK(m.get("https://mewgenics.coopmods.com/update/manifest.json", &body) == 200 && body == "m");
    CHECK(m.inner.asked.back() == "https://mewgenics.ffbcoop.com/update/manifest.json");

    std::printf("a silent name that is the last one left is still asked\n");
    Mirror n;
    n.inner.pages["https://ffbcoop.com/a"]  = {0, ""};
    n.inner.pages["https://coopmods.com/a"] = {0, ""};
    n.inner.pages["https://coopmods.com/b"] = {200, "b"};
    CHECK(n.get("https://coopmods.com/a", &body) == 0);
    CHECK(n.get("https://coopmods.com/b", &body) == 200 && body == "b");
    CHECK(n.inner.asked == v({"https://ffbcoop.com/a", "https://coopmods.com/a", "https://coopmods.com/b"}));
}

void test_dev_login_both_names() {
    std::printf("the dev login goes to the dev prefix on both names, so the fallback carries it\n");
    const Channel& dev = dev_channel();
    const std::string login = "dev:x";
    const std::string want  = "Authorization: Basic " + base64(login);
    CHECK(login_header(dev, login, "https://ffbcoop.com/dev/games.json") == want);
    CHECK(login_header(dev, login, "https://coopmods.com/dev/games.json") == want);
    CHECK(login_header(dev, login, "https://ffbcoop.com/dev/mewgenics/mewcoop.dll") == want);
    CHECK(login_header(dev, login, "https://ffbcoop.com/games.json") == "");
    CHECK(login_header(dev, login, "https://www.ffbcoop.com/dev/games.json") == "");
    CHECK(login_header(dev, login, "https://mewgenics.ffbcoop.com/dev/games.json") == "");
    CHECK(login_header(dev, login, "http://ffbcoop.com/dev/games.json") == "");
    CHECK(login_header(dev, login, "https://ffbcoop.com.evil.test/dev/games.json") == "");
    CHECK(login_header(public_channel(), login, "https://ffbcoop.com/dev/games.json") == "");
}

}  // namespace

int main() {
    test_order();
    test_host();
    test_ffbcoop_answers();
    test_fallback();
    test_no_fallback();
    test_first_answer();
    test_silent_name_skipped();
    test_dev_login_both_names();
    return ffb_test_result();
}
