// Plain substring count.
#include "engine/vector/svg/use_count.h"

namespace pittore::svg {

int countUses(const std::string& s) {
    int n = 0;
    size_t i = 0;
    while ((i = s.find("<use", i)) != std::string::npos) {
        ++n;
        i += 4;
    }
    return n;
}

}  // namespace pittore::svg
