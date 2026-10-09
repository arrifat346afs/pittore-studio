// Double list. Strict: stops at first bad token.
#include "engine/vector/svg/number_list.h"

#include <cstdlib>

namespace pittore::svg {

std::vector<double> parseDoubles(const std::string& s) {
    std::vector<double> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() &&
               (s[i] == ' ' || s[i] == ',' || s[i] == '\t' || s[i] == '\n')) {
            ++i;
        }
        if (i >= s.size()) {
            break;
        }
        char* end = nullptr;
        const double v = std::strtod(s.c_str() + i, &end);
        if (end == s.c_str() + i) {
            break;
        }
        out.push_back(v);
        i = (size_t)(end - s.c_str());
    }
    return out;
}

}  // namespace pittore::svg
