// Drop /* */ runs.
#include "engine/vector/svg/comment_skip.h"

namespace pittore::svg {

std::string stripCssComments(const std::string& s) {
    std::string o;
    size_t i = 0;
    while (i < s.size()) {
        if (s.compare(i, 2, "/*") == 0) {
            const size_t e = s.find("*/", i + 2);
            i = e == std::string::npos ? s.size() : e + 2;
        } else {
            o += s[i++];
        }
    }
    return o;
}

}  // namespace pittore::svg
