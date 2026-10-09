// Exact first-point match closes.
#include "engine/vector/svg/poly_close.h"

namespace pittore::svg {

void closeRing(std::vector<std::pair<double, double>>& pts) {
    if (pts.size() < 3 || pts.front() == pts.back()) {
        return;
    }
    pts.push_back(pts.front());
}

}  // namespace pittore::svg
