// Runs of blanks become one space.
#include "engine/vector/svg/white_wrap.h"

namespace pittore::svg {

std::string collapseWhite(const std::string& s, bool preserve) {
    if (preserve) {
        return s;
    }
    std::string o;
    bool blank = false;
    for (char c : s) {
        const bool sp = c == ' ' || c == '\t' || c == '\n';
        if (sp) {
            if (!blank && !o.empty()) {
                o += ' ';
            }
            blank = true;
        } else {
            o += c;
            blank = false;
        }
    }
    if (!o.empty() && o.back() == ' ') {
        o.pop_back();
    }
    return o;
}

}  // namespace pittore::svg
