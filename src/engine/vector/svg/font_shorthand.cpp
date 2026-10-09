// Last two tokens are size and family.
#include "engine/vector/svg/font_shorthand.h"

namespace pittore::svg {

FontShorthand parseFontShorthand(const std::string& s) {
    FontShorthand o;
    size_t i = 0;
    auto next = [&]() {
        while (i < s.size() && s[i] == ' ') {
            ++i;
        }
        size_t j = i;
        while (j < s.size() && s[j] != ' ') {
            ++j;
        }
        const std::string t = s.substr(i, j > i ? j - i : 0);
        i = j;
        return t;
    };
    std::string t = next();
    if (t == "italic" || t == "oblique" || t == "normal") {
        o.style = t;
        t = next();
    }
    if (t == "bold" || t == "normal" || (!t.empty() && t[0] >= '0' && t[0] <= '9' && t.find("px") == std::string::npos && t.find("pt") == std::string::npos)) {
        // Weight token still pending size check below.
        if (t.find('/') == std::string::npos && t.find("px") == std::string::npos &&
            t.find("pt") == std::string::npos && t.find("em") == std::string::npos) {
            o.weight = t;
            t = next();
        }
    }
    o.size = t;
    while (i < s.size() && s[i] == ' ') {
        ++i;
    }
    o.family = s.substr(i);
    return o;
}

}  // namespace pittore::svg
