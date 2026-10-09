// Map each point.
#include "engine/vector/svg/point_xform.h"

namespace pittore::svg {

std::vector<std::pair<double, double>> xformPoints(
    const std::vector<std::pair<double, double>>& pts, const Affine& m) {
    std::vector<std::pair<double, double>> out;
    out.reserve(pts.size());
    for (const auto& p : pts) {
        out.emplace_back(m.a * p.first + m.c * p.second + m.e,
                         m.b * p.first + m.d * p.second + m.f);
    }
    return out;
}

}  // namespace pittore::svg
