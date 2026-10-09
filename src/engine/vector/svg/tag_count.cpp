// Bounded scan for '<'.
#include "engine/vector/svg/tag_count.h"

namespace pittore::svg {

int countTags(const std::string& s, int cap) {
    int n = 0;
    for (size_t i = 0; i < s.size() && n < cap; ++i) {
        if (s[i] == '<') {
            ++n;
        }
    }
    return n;
}

}  // namespace pittore::svg
