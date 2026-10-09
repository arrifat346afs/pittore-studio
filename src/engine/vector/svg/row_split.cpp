// Contiguous split with floor.
#include "engine/vector/svg/row_split.h"

namespace pittore::svg {

std::vector<std::pair<int, int>> splitRows(int rows, int threads, int minPer) {
    std::vector<std::pair<int, int>> out;
    if (rows <= 0 || threads <= 0) {
        return out;
    }
    int parts = rows / minPer;
    if (parts < 1) {
        parts = 1;
    }
    if (parts > threads) {
        parts = threads;
    }
    if (parts > rows) {
        parts = rows;
    }
    const int base = rows / parts;
    const int rest = rows % parts;
    int cur = 0;
    for (int t = 0; t < parts; ++t) {
        const int n = base + (t < rest ? 1 : 0);
        out.emplace_back(cur, cur + n);
        cur += n;
    }
    return out;
}

}  // namespace pittore::svg
