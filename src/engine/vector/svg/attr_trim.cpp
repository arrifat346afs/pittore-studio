// Both ends.
#include "engine/vector/svg/attr_trim.h"

namespace pittore::svg {

std::string trimAttr(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\n')) {
        ++a;
    }
    size_t b = s.size();
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\n')) {
        --b;
    }
    return s.substr(a, b - a);
}

}  // namespace pittore::svg
