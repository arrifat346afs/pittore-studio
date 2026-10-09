// ASCII case only.
#include "engine/vector/svg/text_xform.h"

#include <cctype>

namespace pittore::svg {

std::string applyTextTransform(const std::string& s, const std::string& mode) {
    if (mode.empty() || mode == "none") {
        return s;
    }
    std::string o = s;
    if (mode == "uppercase") {
        for (char& c : o) {
            c = (char)std::toupper((unsigned char)c);
        }
    } else if (mode == "lowercase") {
        for (char& c : o) {
            c = (char)std::tolower((unsigned char)c);
        }
    } else if (mode == "capitalize" && !o.empty()) {
        o[0] = (char)std::toupper((unsigned char)o[0]);
    }
    return o;
}

}  // namespace pittore::svg
