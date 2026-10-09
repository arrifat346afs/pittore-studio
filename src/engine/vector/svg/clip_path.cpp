// Ray cast test. Counts edge crossings.
#include "engine/vector/svg/clip_path.h"

namespace pittore::svg {

bool pointInPoly(const std::vector<std::pair<double, double>>& pts, double x,
                 double y, bool evenOdd) {
    if (pts.size() < 3) {
        return false;
    }
    int winding = 0;
    bool inside = false;
    for (size_t k = 0; k < pts.size(); ++k) {
        const auto [x0, y0] = pts[k];
        const auto [x1, y1] = pts[(k + 1) % pts.size()];
        if ((y0 > y) == (y1 > y)) {
            continue;
        }
        const double at = x0 + (y - y0) * (x1 - x0) / (y1 - y0);
        if (at > x) {
            inside = !inside;
            winding += y0 < y1 ? 1 : -1;
        }
    }
    return evenOdd ? inside : winding != 0;
}

bool pointInClip(const std::vector<ClipPoly>& clip, double x, double y) {
    for (const auto& p : clip) {
        if (pointInPoly(p.pts, x, y, p.evenOdd)) {
            return true;
        }
    }
    return false;
}

}  // namespace pittore::svg
