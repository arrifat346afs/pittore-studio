// Snap selection: grid, guide, alignment and distribution targets.
#include "engine/vector/snap.h"

#include <cmath>

namespace pittore::vector {

SnapResult snapPoint(double px, double py, const std::vector<SnapCandidate>& cands,
                     double tolerance, unsigned enabled) {
    SnapResult best{px, py, SnapNone, 1e100, false};
    for (const auto& c : cands) {
        if (!(c.target & enabled)) continue;
        double dx = c.x - px, dy = c.y - py;
        double d = std::sqrt(dx * dx + dy * dy) * c.weight;
        if (d <= tolerance && d < best.distance) {
            best = SnapResult{c.x, c.y, c.target, d, true};
            // Exact hits win immediately (bbox-vs-path tie goes to path:
            // path candidates carry weight < 1 from their builder).
            if (d == 0) break;
        }
    }
    return best;
}

std::vector<SnapCandidate> gridCandidates(double px, double py, double step, double ox,
                                          double oy) {
    if (step <= 0) return {};
    double gx = ox + std::round((px - ox) / step) * step;
    double gy = oy + std::round((py - oy) / step) * step;
    return {SnapCandidate{gx, gy, SnapGrid, 1.0}};
}

std::vector<SnapCandidate> guideCandidates(double px, double py,
                                            const std::vector<double>& vGuides,
                                            const std::vector<double>& hGuides) {
    std::vector<SnapCandidate> out;
    for (double gx : vGuides) out.push_back(SnapCandidate{gx, py, SnapGuide, 0.95});
    for (double gy : hGuides) out.push_back(SnapCandidate{px, gy, SnapGuide, 0.95});
    return out;
}

std::vector<SnapCandidate> nodeCandidates(
    const std::vector<std::pair<double, double>>& anchors) {
    std::vector<SnapCandidate> out;
    for (auto [x, y] : anchors) out.push_back(SnapCandidate{x, y, SnapNode, 0.9});
    return out;
}

std::vector<SnapCandidate> bboxCandidates(double x0, double y0, double x1, double y1) {
    std::vector<SnapCandidate> out;
    double mx = (x0 + x1) / 2, my = (y0 + y1) / 2;
    for (auto [x, y, t] : std::vector<std::tuple<double, double, unsigned>>{
             {x0, y0, SnapBbox}, {x1, y0, SnapBbox}, {x0, y1, SnapBbox},
             {x1, y1, SnapBbox}, {mx, y0, SnapMidpoint}, {mx, y1, SnapMidpoint},
             {x0, my, SnapMidpoint}, {x1, my, SnapMidpoint}, {mx, my, SnapCenter}}) {
        double w = (t == SnapCenter) ? 0.92 : 1.0;
        out.push_back(SnapCandidate{x, y, t, w});
    }
    return out;
}

std::vector<SnapCandidate> intersectionCandidates(
    const std::vector<std::pair<double, double>>& a,
    const std::vector<std::pair<double, double>>& b, double tolerance) {
    std::vector<SnapCandidate> out;
    auto segX = [&](const std::pair<double, double>& p0,
                    const std::pair<double, double>& p1,
                    const std::pair<double, double>& q0,
                    const std::pair<double, double>& q1, double& ix, double& iy) -> bool {
        double d = (p1.first - p0.first) * (q1.second - q0.second) -
                   (p1.second - p0.second) * (q1.first - q0.first);
        if (std::abs(d) < 1e-12) return false;
        double t = ((q0.first - p0.first) * (q1.second - q0.second) -
                    (q0.second - p0.second) * (q1.first - q0.first)) /
                   d;
        double u = ((q0.first - p0.first) * (p1.second - p0.second) -
                    (q0.second - p0.second) * (p1.first - p0.first)) /
                   d;
        if (t < -1e-9 || t > 1 + 1e-9 || u < -1e-9 || u > 1 + 1e-9) return false;
        ix = p0.first + t * (p1.first - p0.first);
        iy = p0.second + t * (p1.second - p0.second);
        return true;
    };
    for (size_t i = 0; i + 1 < a.size(); i++)
        for (size_t j = 0; j + 1 < b.size(); j++) {
            double ix, iy;
            if (segX(a[i], a[i + 1], b[j], b[j + 1], ix, iy)) {
                bool dup = false;
                for (auto& c : out)
                    if (std::hypot(c.x - ix, c.y - iy) < tolerance) {
                        dup = true;
                        break;
                    }
                if (!dup) out.push_back(SnapCandidate{ix, iy, SnapIntersection, 0.85});
            }
        }
    return out;
}

}  // namespace pittore::vector
