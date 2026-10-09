// EF BB BF prefix.
#include "engine/vector/svg/bom_strip.h"

namespace pittore::svg {

std::string stripBom(const std::string& s) {
    if (s.size() >= 3 && (unsigned char)s[0] == 0xef &&
        (unsigned char)s[1] == 0xbb && (unsigned char)s[2] == 0xbf) {
        return s.substr(3);
    }
    return s;
}

}  // namespace pittore::svg
