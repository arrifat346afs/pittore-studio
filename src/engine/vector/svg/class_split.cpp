// Blanks split.
#include "engine/vector/svg/class_split.h"

namespace pittore::svg {

std::vector<std::string> splitClasses(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && s[i] == ' ') {
            ++i;
        }
        size_t j = i;
        while (j < s.size() && s[j] != ' ') {
            ++j;
        }
        if (j > i) {
            out.push_back(s.substr(i, j - i));
        }
        i = j;
    }
    return out;
}

}  // namespace pittore::svg
