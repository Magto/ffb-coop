// test_channel.cpp -- the two channels and the dev login rule (#26, docs/SPEC.md
// "Dev channel"): what each channel reads and where it installs, the base64 of
// the Basic login, and that the login goes to dev channel URLs and nowhere else.
//
// The URLs and names are written out from docs/SPEC.md; the base64 values are
// RFC 4648 section 10's test vectors and RFC 7617 section 2's example.
#include "channel.h"
#include "ffb_test.h"

#include <cstring>
#include <string>

using namespace ffb;

namespace {

bool same(const char* a, const char* b) { return a && b && std::strcmp(a, b) == 0; }

}  // namespace

int main() {
    const Channel& pub = public_channel();
    const Channel& dev = dev_channel();

    std::printf("the public channel is what FFB Co-op.exe always read\n");
    CHECK(same(pub.exe_name, "FFB Co-op.exe"));
    CHECK(same(pub.games_json_url, "https://coopmods.com/games.json"));
    CHECK(same(pub.package_dir, "FFB Co-op"));
    CHECK(pub.login_prefix == nullptr);
    CHECK(pub.launcher_env == nullptr);

    std::printf("the dev channel: its own exe name, games.json, folder and login prefix\n");
    CHECK(same(dev.exe_name, "FFB Co-op - dev.exe"));
    CHECK(same(dev.games_json_url, "https://coopmods.com/dev/games.json"));
    CHECK(same(dev.package_dir, "FFB Co-op dev"));
    CHECK(same(dev.login_prefix, "https://coopmods.com/dev/"));
    CHECK(same(dev.launcher_env, "MEWCOOP_NOUPDATE=1"));

    std::printf("this test is built without FFB_DEV_CHANNEL: this_channel() is the public one\n");
    CHECK(&this_channel() == &pub);

    std::printf("base64: RFC 4648 section 10\n");
    CHECK(base64("") == "");
    CHECK(base64("f") == "Zg==");
    CHECK(base64("fo") == "Zm8=");
    CHECK(base64("foo") == "Zm9v");
    CHECK(base64("foob") == "Zm9vYg==");
    CHECK(base64("fooba") == "Zm9vYmE=");
    CHECK(base64("foobar") == "Zm9vYmFy");
    CHECK(base64(std::string("\xff\xfe\x00", 3)) == "//4A");

    std::printf("the login header: RFC 7617's example, on a dev URL\n");
    const std::string login = "Aladdin:open sesame";
    CHECK(login_header(dev, login, "https://coopmods.com/dev/games.json") ==
          "Authorization: Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ==");
    CHECK(login_header(dev, login, "https://coopmods.com/dev/launcher/FFB%20Co-op%20-%20dev.exe") != "");
    CHECK(login_header(dev, login, "https://coopmods.com/dev/mewgenics/mewcoop.dll") != "");

    std::printf("never anywhere but under the dev prefix\n");
    CHECK(login_header(dev, login, "https://coopmods.com/games.json") == "");
    CHECK(login_header(dev, login, "https://coopmods.com/launcher/FFB%20Co-op.exe") == "");
    CHECK(login_header(dev, login, "https://mewgenics.coopmods.com/update/manifest.json") == "");
    CHECK(login_header(dev, login, "http://coopmods.com/dev/games.json") == "");          // not in the clear
    CHECK(login_header(dev, login, "https://coopmods.com/devx/games.json") == "");
    CHECK(login_header(dev, login, "https://coopmods.com/dev") == "");
    CHECK(login_header(dev, login, "https://coopmods.com.evil.test/dev/games.json") == "");
    CHECK(login_header(dev, login, "https://evil.test/https://coopmods.com/dev/") == "");

    std::printf("the public channel never sends one, whatever the URL\n");
    CHECK(login_header(pub, login, "https://coopmods.com/dev/games.json") == "");
    CHECK(login_header(pub, login, "https://coopmods.com/games.json") == "");

    std::printf("no login, or no user name: no header\n");
    CHECK(login_header(dev, "", "https://coopmods.com/dev/games.json") == "");
    CHECK(login_header(dev, "nocolon", "https://coopmods.com/dev/games.json") == "");
    CHECK(login_header(dev, ":password", "https://coopmods.com/dev/games.json") == "");

    std::printf("the process login: nothing until the dev exe sets it, and only for its prefix\n");
    CHECK(process_login_header("https://coopmods.com/dev/games.json") == "");
    set_process_login(dev, login);
    CHECK(process_login_header("https://coopmods.com/dev/games.json") ==
          "Authorization: Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ==");
    CHECK(process_login_header("https://coopmods.com/games.json") == "");
    set_process_login(dev, "");
    CHECK(process_login_header("https://coopmods.com/dev/games.json") == "");

    std::printf("the one dev credential: user name dev, the shared password after the colon\n");
    CHECK(same(kDevLoginUser, "dev"));
    // base64("dev:x") worked by hand: "dev" -> ZGV2, ":x" (0x3a 0x78) -> Ong=.
    CHECK(login_header(dev, std::string(kDevLoginUser) + ":x", "https://coopmods.com/dev/games.json") ==
          "Authorization: Basic ZGV2Ong=");
    CHECK(login_header(pub, std::string(kDevLoginUser) + ":x", "https://coopmods.com/dev/games.json") == "");

    return ffb_test_result();
}
