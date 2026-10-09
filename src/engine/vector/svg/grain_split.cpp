// Items over grain, clamped to hw.
#include "engine/vector/svg/grain_split.h"

namespace pittore::svg {

unsigned pickThreads(std::uint32_t items, std::uint32_t grain, unsigned hw) {
    if (hw == 0) {
        hw = 4;
    }
    if (grain < 1) {
        grain = 1;
    }
    unsigned t = items / grain;
    if (t < 1) {
        t = 1;
    }
    if (t > hw) {
        t = hw;
    }
    return t;
}

}  // namespace pittore::svg
