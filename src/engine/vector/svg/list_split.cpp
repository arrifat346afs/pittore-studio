// Trim each part.
#include "engine/vector/svg/list_split.h"

#include "engine/vector/svg/attr_trim.h"

namespace pittore::svg {

std::vector<std::string> splitComma(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i <= s.size()) {
        const size_t p = s.find(',', i);
        const std::string one =
            trimAttr(s.substr(i, p == std::string::npos ? std::string::npos
                                                       : p - i));
        if (!one.empty()) {
            out.push_back(one);
        }
        if (p == std::string::npos) {
            break;
        }
        i = p + 1;
    }
    return out;
}

}  // namespace pittore::svg
