// & < only.
#include "engine/vector/svg/text_escape.h"

namespace pittore::svg {

std::string escapeText(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '&') {
            o += "&amp;";
        } else if (c == '<') {
            o += "&lt;";
        } else {
            o += c;
        }
    }
    return o;
}

}  // namespace pittore::svg
