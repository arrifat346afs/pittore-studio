// & < " only.
#include "engine/vector/svg/attr_escape.h"

namespace pittore::svg {

std::string escapeAttr(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '&') {
            o += "&amp;";
        } else if (c == '<') {
            o += "&lt;";
        } else if (c == '"') {
            o += "&quot;";
        } else {
            o += c;
        }
    }
    return o;
}

}  // namespace pittore::svg
