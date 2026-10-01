// package.cpp -- see package.h for the shape of a run.
//
// Ported from Magto/mewgenics-coop loader/mewcoop_update.cpp (MIT, Copyright (c)
// 2026 SanTertrust; Copyright (c) 2026 Martin Strålenhielm for the Mewgenics Coop
// fork): the compare-then-stage-then-swap order and sibling_path. The name
// allowlist there becomes the plain-file-name rule here, since every game's
// package carries its own names.
#include "package.h"

#include <filesystem>
#include <fstream>
#include <system_error>

#include "json.hpp"
#include "plain_name.h"

namespace fs = std::filesystem;

namespace ffb {

namespace {

void say(std::FILE* log, const char* fmt, const std::string& a, const std::string& b = std::string()) {
    if (!log) return;
    std::fprintf(log, fmt, a.c_str(), b.c_str());
    std::fputc('\n', log);
}

std::string human(std::uint64_t bytes) {
    char buf[32];
    if (bytes >= (10ull << 20))     std::snprintf(buf, sizeof(buf), "%llu MB", (unsigned long long)(bytes >> 20));
    else if (bytes >= (1ull << 20)) std::snprintf(buf, sizeof(buf), "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    else                            std::snprintf(buf, sizeof(buf), "%llu KB", (unsigned long long)(bytes >> 10));
    return buf;
}

// "N" or "X.Y.Z", each part decimal digits.
bool is_manifest_version(const std::string& v) {
    int parts = 1;
    bool digit = false;
    for (char c : v) {
        if (c == '.') {
            if (!digit) return false;
            ++parts; digit = false;
        } else if (c >= '0' && c <= '9') {
            digit = true;
        } else {
            return false;
        }
    }
    return digit && (parts == 1 || parts == 3);
}

bool is_https_url(const std::string& url) {
    const std::string prefix = "https://";
    return url.size() > prefix.size() && ascii_lower(url.substr(0, prefix.size())) == prefix &&
           url[prefix.size()] != '/';
}

std::string percent_encode(const std::string& s) {
    static const char digits[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '.' || c == '_' || c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += digits[c >> 4];
            out += digits[c & 0xf];
        }
    }
    return out;
}

// One file the manifest lists that is not already current in the folder.
struct Want {
    const ManifestFile* file;
    fs::path            local;    // <dir>\<name>
    fs::path            staged;   // <dir>\<name>.new
    bool                fetched = false;
};

// Downloads `w` to its .new, checking size and sha256 as the bytes arrive.
// -> empty on success, else why it failed; the .new is gone on failure.
std::string fetch(Net& net, const std::string& url, Want& w, std::FILE* log) {
    const ManifestFile& f = *w.file;
    std::ofstream out(w.staged, std::ios::binary | std::ios::trunc);
    if (!out) return "could not write " + f.name + ".new";

    Sha256        sha;
    std::uint64_t got = 0;
    bool          too_big = false, write_failed = false;
    int           last_pct = -1;
    const bool    progress = log && f.size > (4ull << 20);
    const int status = net.get(url, [&](const char* p, std::size_t n) {
        if (got + n > f.size) { too_big = true; return false; }
        out.write(p, (std::streamsize)n);
        if (!out) { write_failed = true; return false; }
        sha.feed(p, n);
        got += n;
        // One line that rewrites itself, and only for a download big enough that
        // a silent minute would look like a hang.
        if (progress) {
            const int pct = (int)(got * 100 / f.size);
            if (pct != last_pct && pct % 5 == 0) {
                std::fprintf(log, "\r[*] %s  %3d%%", f.name.c_str(), pct);
                last_pct = pct;
            }
        }
        return true;
    });
    if (progress) std::fprintf(log, "\r%40s\r", "");
    out.close();

    std::string why;
    if (status != 200)
        why = status == 0 ? "no answer from the server" : "the server said HTTP " + std::to_string(status);
    else if (write_failed || out.fail())
        why = "could not write " + f.name + ".new";
    else if (too_big)
        why = "is larger than the " + std::to_string(f.size) + " bytes the manifest says";
    else if (got != f.size)
        why = "is " + std::to_string(got) + " bytes, the manifest says " + std::to_string(f.size);
    else if (!same_sha256(sha.finish_hex(), f.sha256))
        why = "does not match its sha256";

    if (!why.empty()) {
        std::error_code ec;
        fs::remove(w.staged, ec);
    }
    return why;
}

}  // namespace

bool parse_manifest(const std::string& text, Manifest* out, std::string* why) {
    auto reject = [why](const std::string& reason) {
        if (why) *why = reason;
        return false;
    };
    const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded()) return reject("the manifest is not JSON");
    if (!j.is_object()) return reject("the manifest is not a JSON object");

    Manifest m;
    const auto v = j.find("version");
    if (v == j.end() || !v->is_string()) return reject("\"version\" is missing or not a string");
    m.version = v->get<std::string>();
    if (!is_manifest_version(m.version)) return reject("\"version\" is neither N nor X.Y.Z: " + m.version);

    const auto files = j.find("files");
    if (files == j.end() || !files->is_array()) return reject("\"files\" is missing or not an array");
    if (files->empty()) return reject("\"files\" is empty");

    std::vector<std::string> seen;
    for (const auto& e : *files) {
        if (!e.is_object()) return reject("an entry in \"files\" is not an object");
        ManifestFile f;

        const auto n = e.find("name");
        if (n == e.end() || !n->is_string()) return reject("a file has no \"name\" string");
        f.name = n->get<std::string>();
        std::string name_why;
        if (!is_plain_file_name(f.name, &name_why))
            return reject("rejected file name \"" + f.name + "\": it " + name_why);
        const std::string key = ascii_lower(f.name);
        for (const auto& s : seen)
            if (s == key) return reject("file name \"" + f.name + "\" is listed twice");
        seen.push_back(key);

        const auto s = e.find("size");
        if (s == e.end() || !s->is_number_integer()) return reject(f.name + ": \"size\" is missing or not an integer");
        if (s->is_number_unsigned()) f.size = s->get<std::uint64_t>();
        else if (s->get<std::int64_t>() > 0) f.size = (std::uint64_t)s->get<std::int64_t>();
        if (f.size < 1) return reject(f.name + ": \"size\" is less than 1");

        const auto h = e.find("sha256");
        if (h == e.end() || !h->is_string()) return reject(f.name + ": \"sha256\" is missing or not a string");
        f.sha256 = h->get<std::string>();
        if (!is_sha256_hex(f.sha256)) return reject(f.name + ": \"sha256\" is not 64 hex digits");

        const auto r = e.find("required");
        if (r != e.end()) {
            if (!r->is_boolean()) return reject(f.name + ": \"required\" is not true or false");
            f.required = r->get<bool>();
        }
        m.files.push_back(f);
    }
    *out = m;
    return true;
}

std::string sibling_url(const std::string& manifest_url, const std::string& name) {
    std::string base = manifest_url.substr(0, manifest_url.find_first_of("?#"));
    const std::size_t scheme = base.find("://");
    const std::size_t host_start = scheme == std::string::npos ? 0 : scheme + 3;
    const std::size_t path_start = base.find('/', host_start);
    if (path_start == std::string::npos) base += '/';
    else base.erase(base.rfind('/') + 1);
    return base + percent_encode(name);
}

PackageResult update_package(const std::string& package_dir, const std::string& manifest_url,
                             const std::string& required_launcher, Net& net, std::FILE* log,
                             const std::vector<PublicKey>& keys) {
    PackageResult res;

    // --- 1. the manifest ---
    if (!is_https_url(manifest_url)) {
        res.outcome = PackageOutcome::Invalid;
        res.reason  = "the manifest URL is not https: " + manifest_url;
        return res;
    }
    std::string body;
    const int status = get_body(net, manifest_url, &body);
    if (status != 200) {
        res.outcome = PackageOutcome::Unreachable;
        res.reason  = status == 0 ? "no answer from " + manifest_url
                                  : manifest_url + " said HTTP " + std::to_string(status);
        return res;
    }
    // Its signature, checked over the exact bytes before any of them is parsed.
    const std::string sig_url = signature_url(manifest_url);
    std::string sig;
    const int sig_status = get_body(net, sig_url, &sig);
    if (sig_status == 0) {
        res.outcome = PackageOutcome::Unreachable;
        res.reason  = "no answer from " + sig_url;
        return res;
    }
    if (sig_status != 200) {
        res.outcome = PackageOutcome::Unsigned;
        res.reason  = "the manifest is not signed: " + sig_url + " said HTTP " + std::to_string(sig_status);
        return res;
    }
    res.reason = check_signature("the manifest", body, sig, keys);
    if (!res.reason.empty()) {
        res.outcome = PackageOutcome::Unsigned;
        return res;
    }
    Manifest m;
    if (!parse_manifest(body, &m, &res.reason)) {
        res.outcome = PackageOutcome::Invalid;
        return res;
    }
    res.version       = m.version;
    res.manifest_text = body;
    res.manifest      = m;
    if (!required_launcher.empty()) {
        bool found = false;
        for (const auto& f : m.files)
            if (ascii_lower(f.name) == ascii_lower(required_launcher) && f.required) found = true;
        if (!found) {
            res.outcome = PackageOutcome::Invalid;
            res.reason  = "the manifest does not list " + required_launcher + " as a required file";
            return res;
        }
    }

    // --- 2. which files differ ---
    const fs::path dir = fs::u8path(package_dir);
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (!fs::is_directory(dir, ec)) {
        res.outcome = PackageOutcome::Failed;
        res.reason  = "could not create " + package_dir;
        return res;
    }
    std::vector<Want> wants;
    for (const auto& f : m.files) {
        Want w{&f, dir / fs::u8path(f.name), dir / fs::u8path(f.name + ".new")};
        std::string   have;
        std::uint64_t have_size = 0;
        if (sha256_file(w.local.u8string(), &have, &have_size) && have_size == f.size && same_sha256(have, f.sha256))
            continue;   // already current: silent, this is every ordinary start
        wants.push_back(w);
    }
    if (wants.empty()) {
        res.outcome = PackageOutcome::Current;
        return res;
    }

    // --- 3. download everything, check everything, replace nothing yet ---
    for (auto& w : wants) {
        const ManifestFile& f = *w.file;
        say(log, "[*] downloading %s (%s)", f.name, human(f.size));
        const std::string why = fetch(net, sibling_url(manifest_url, f.name), w, log);
        if (why.empty()) { w.fetched = true; continue; }
        if (!f.required) {
            res.warnings.push_back(f.name + " " + why + " -- skipped, it is optional");
            say(log, "[!] %s %s -- skipped, it is optional", f.name, why);
            continue;
        }
        for (auto& other : wants) fs::remove(other.staged, ec);
        res.outcome = PackageOutcome::Failed;
        res.reason  = f.name + " " + why + " -- nothing was replaced";
        say(log, "[!] %s", res.reason);
        return res;
    }

    // --- 4. replace ---
    std::string replace_failed;
    for (auto& w : wants) {
        if (!w.fetched) continue;
        const ManifestFile& f = *w.file;
        fs::rename(w.staged, w.local, ec);
        if (!ec) {
            res.updated.push_back(f.name);
            say(log, "[+] %s updated", f.name);
            continue;
        }
        // The game holding the file open is the realistic cause. The old file
        // stays where it was.
        const std::string why = "could not replace " + f.name + " (" + ec.message() + ") -- is the game running?";
        std::error_code rm;
        fs::remove(w.staged, rm);
        say(log, "[!] %s", why);
        if (f.required) replace_failed += (replace_failed.empty() ? "" : "; ") + why;
        else            res.warnings.push_back(why);
    }
    if (!replace_failed.empty()) {
        res.outcome = PackageOutcome::Failed;
        res.reason  = replace_failed;
        return res;
    }
    res.outcome = PackageOutcome::Current;
    return res;
}

}  // namespace ffb
