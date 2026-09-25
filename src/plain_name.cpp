// plain_name.cpp -- see plain_name.h.
#include "plain_name.h"

#include <cstddef>

namespace ffb {

std::string ascii_lower(const std::string& s) {
    std::string out = s;
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

bool ends_with_exe(const std::string& name) {
    return name.size() >= 4 && ascii_lower(name.substr(name.size() - 4)) == ".exe";
}

// Characters, not bytes: a UTF-8 continuation byte (10xxxxxx) does not start one.
static std::size_t utf8_length(const std::string& s) {
    std::size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++n;
    return n;
}

// CON, PRN, AUX, NUL, COM1-COM9, LPT1-LPT9, with or without an extension:
// Windows opens the device for "nul.txt" just as for "nul".
static bool is_reserved_device_name(const std::string& name) {
    const std::string base = ascii_lower(name.substr(0, name.find('.')));
    if (base == "con" || base == "prn" || base == "aux" || base == "nul") return true;
    if (base.size() == 4 && (base.compare(0, 3, "com") == 0 || base.compare(0, 3, "lpt") == 0) &&
        base[3] >= '1' && base[3] <= '9')
        return true;
    return false;
}

bool is_plain_file_name(const std::string& name, std::string* why) {
    auto reject = [why](const char* reason) {
        if (why) *why = reason;
        return false;
    };
    if (name.empty()) return reject("is empty");
    if (utf8_length(name) > 255) return reject("is longer than 255 characters");
    for (unsigned char c : name) {
        if (c == '/') return reject("contains /");
        if (c == '\\') return reject("contains \\");
        if (c == ':') return reject("contains :");
        if (c < 0x20) return reject("contains a control character");
    }
    if (name == "." || name == "..") return reject("is . or ..");
    if (name.find("..") != std::string::npos) return reject("contains ..");
    if (name.front() == ' ') return reject("starts with a space");
    if (name.back() == ' ') return reject("ends with a space");
    if (name.back() == '.') return reject("ends with .");
    if (is_reserved_device_name(name)) return reject("is a Windows reserved device name");
    return true;
}

}  // namespace ffb
