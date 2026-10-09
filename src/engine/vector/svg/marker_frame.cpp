// Vertex frames. Ends use one segment, mids average.
#include "engine/vector/svg/marker_frame.h"

#include <cmath>

namespace pittore::svg {

std::vector<MarkerFrame> framesForPolyline(
    const std::vector<std::pair<double, double>>& pts, bool closed) {
    std::vector<MarkerFrame> out;
    if (pts.size() < 2) {
        return out;
    }
    auto ang = [](double x0, double y0, double x1, double y1) {
        return std::atan2(y1 - y0, x1 - x0) * 180.0 / 3.141592653589793;
    };
    for (size_t k = 0; k < pts.size(); ++k) {
        size_t a = k == 0 ? (closed ? pts.size() - 1 : 0) : k - 1;
        size_t b = k + 1 >= pts.size() ? (closed ? 0 : pts.size() - 1) : k + 1;
        const double a1 = ang(pts[a].first, pts[a].second, pts[k].first,
                              pts[k].second);
        const double a2 = ang(pts[k].first, pts[k].second, pts[b].first,
                              pts[b].second);
        double m = (a1 + a2) * 0.5;
        if (k == 0 && !closed) {
            m = a2;
        }
        if (k + 1 == pts.size() && !closed) {
            m = a1;
        }
        out.push_back(MarkerFrame{pts[k].first, pts[k].second, m});
    }
    return out;
}

}  // namespace pittore::svg
