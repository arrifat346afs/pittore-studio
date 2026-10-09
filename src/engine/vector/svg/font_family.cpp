// Comma split, quotes trimmed.
#include "engine/vector/svg/font_family.h"

namespace pittore::svg {

std::vector<std::string> splitFamilies(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == ',')) {
            ++i;
        }
        size_t j = i;
        while (j < s.size() && s[j] != ',') {
            ++j;
        }
        size_t b = j;
        while (b > i && s[b - 1] == ' ') {
            --b;
        }
        std::string one = s.substr(i, b > i ? b - i : 0);
        if (!one.empty() && one.front() == '"' && one.back() == '"' && one.size() > 1) {
            one = one.substr(1, one.size() - 2);
        }
        if (!one.empty() && one.front() == '\'' && one.back() == '\'' && one.size() > 1) {
            one = one.substr(1, one.size() - 2);
        }
        if (!one.empty()) {
            out.push_back(one);
        }
        i = j;
    }
    return out;
}

std::string firstFamily(const std::string& s) {
    const auto v = splitFamilies(s);
    return v.empty() ? std::string() : v[0];
}

}  // namespace pittore::svg
