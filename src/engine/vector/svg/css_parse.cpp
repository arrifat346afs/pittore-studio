// Style list and sheet parser. order kept, later wins.
#include "engine/vector/svg/css_parse.h"

#include <cctype>

namespace pittore::svg {
namespace {

// Trim blanks on both ends.
std::string trim(std::string s) {
    size_t a = 0;
    while (a < s.size() && std::isspace((unsigned char)s[a])) {
        ++a;
    }
    size_t b = s.size();
    while (b > a && std::isspace((unsigned char)s[b - 1])) {
        --b;
    }
    return s.substr(a, b - a);
}

}  // namespace

std::map<std::string, std::string> parseStyleDecls(const std::string& style) {
    std::map<std::string, std::string> out;
    size_t i = 0;
    while (i < style.size()) {
        while (i < style.size() &&
               (std::isspace((unsigned char)style[i]) || style[i] == ';')) {
            ++i;
        }
        const size_t k0 = i;
        while (i < style.size() && style[i] != ':' && style[i] != ';') {
            ++i;
        }
        if (i >= style.size() || style[i] != ':') {
            continue;
        }
        const std::string key = trim(style.substr(k0, i - k0));
        ++i;
        const size_t v0 = i;
        while (i < style.size() && style[i] != ';') {
            ++i;
        }
        const std::string val = trim(style.substr(v0, i - v0));
        if (!key.empty()) {
            out[key] = val;
        }
    }
    return out;
}

std::vector<CssRule> parseStylesheet(const std::string& css) {
    std::vector<CssRule> rules;
    size_t i = 0;
    while (i < css.size()) {
        // Skip comments.
        if (css.compare(i, 2, "/*") == 0) {
            const size_t end = css.find("*/", i + 2);
            i = end == std::string::npos ? css.size() : end + 2;
            continue;
        }
        if (std::isspace((unsigned char)css[i])) {
            ++i;
            continue;
        }
        const size_t bOpen = css.find('{', i);
        if (bOpen == std::string::npos) {
            break;
        }
        const std::string sel = css.substr(i, bOpen - i);
        const size_t bClose = css.find('}', bOpen);
        if (bClose == std::string::npos) {
            break;
        }
        const auto decls = parseStyleDecls(css.substr(bOpen + 1, bClose - bOpen - 1));
        size_t s0 = 0;
        while (s0 < sel.size()) {
            const size_t comma = sel.find(',', s0);
            const std::string one = trim(sel.substr(
                s0, comma == std::string::npos ? std::string::npos : comma - s0));
            if (!one.empty()) {
                rules.push_back(CssRule{one, decls});
            }
            if (comma == std::string::npos) {
                break;
            }
            s0 = comma + 1;
        }
        i = bClose + 1;
    }
    return rules;
}

}  // namespace pittore::svg
