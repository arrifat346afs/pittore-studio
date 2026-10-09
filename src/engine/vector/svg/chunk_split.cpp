// Contiguous split plus px gate.
#include "engine/vector/svg/chunk_split.h"

namespace pittore::svg {

std::vector<std::pair<int, int>> splitRanges(int count, int parts) {
    std::vector<std::pair<int, int>> out;
    if (count <= 0 || parts <= 0) {
        return out;
    }
    if (parts > count) {
        parts = count;
    }
    const int base = count / parts;
    const int rest = count % parts;
    int cur = 0;
    for (int t = 0; t < parts; ++t) {
        const int n = base + (t < rest ? 1 : 0);
        out.emplace_back(cur, cur + n);
        cur += n;
    }
    return out;
}

bool needsThreads(int pxCount) {
    return pxCount > 2048;
}

}  // namespace pittore::svg
