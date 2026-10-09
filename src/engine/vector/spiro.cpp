// Spiro/B-spline fitting (interactive-rate subset of a full solver).
#include "engine/vector/spiro.h"

namespace pittore::vector {

std::vector<Segment> spiroFit(const std::vector<SpiroPoint>& pts, bool closed) {
    std::vector<Segment> out;
    if (pts.empty()) return out;
    if (pts.size() == 1) {
        out.push_back(Segment{Segment::Kind::MoveTo, (float)pts[0].x, (float)pts[0].y});
        return out;
    }
    // Fairing passes: Laplacian relax smooth runs (corners pin the ends),
    // converging toward minimum-curvature-variation placement before the
    // Catmull-Rom emission. Deterministic; corners never drift.
    std::vector<SpiroPoint> fair = pts;
    size_t n = fair.size();
    int passes = (int)std::min<size_t>(12, 3 + n / 8);
    for (int pass = 0; pass < passes; pass++) {
        std::vector<SpiroPoint> next = fair;
        for (size_t i = 0; i < n; i++) {
            if (fair[i].type == SpiroType::Corner) continue;
            size_t a = (i + n - 1) % n, b = (i + 1) % n;
            if (!closed && (i == 0 || i + 1 == n)) continue;
            if (fair[a].type == SpiroType::Corner && fair[b].type == SpiroType::Corner)
                continue;
            double mx = (fair[a].x + fair[b].x) / 2, my = (fair[a].y + fair[b].y) / 2;
            next[i].x = fair[i].x + (mx - fair[i].x) * 0.5;
            next[i].y = fair[i].y + (my - fair[i].y) * 0.5;
        }
        fair = next;
    }
    out.push_back(Segment{Segment::Kind::MoveTo, (float)fair[0].x, (float)fair[0].y});
    size_t last = closed ? n : n - 1;
    for (size_t i = 0; i < last; i++) {
        const auto& p0 = fair[(i + n - 1) % n];
        const auto& p1 = fair[i % n];
        const auto& p2 = fair[(i + 1) % n];
        const auto& p3 = fair[(i + 2) % n];
        bool corner = p1.type == SpiroType::Corner || p2.type == SpiroType::Corner;
        if (corner) {
            out.push_back(
                Segment{Segment::Kind::LineTo, (float)p2.x, (float)p2.y});
            continue;
        }
        double c1x = p1.x + (p2.x - p0.x) / 6.0;
        double c1y = p1.y + (p2.y - p0.y) / 6.0;
        double c2x = p2.x - (p3.x - p1.x) / 6.0;
        double c2y = p2.y - (p3.y - p1.y) / 6.0;
        out.push_back(Segment{Segment::Kind::CubicTo, (float)c1x, (float)c1y, (float)c2x,
                              (float)c2y, (float)p2.x, (float)p2.y});
    }
    if (closed) out.push_back(Segment{Segment::Kind::Close});
    return out;
}

std::vector<Segment> bsplineFit(const std::vector<std::pair<double, double>>& pts,
                                bool closed) {
    std::vector<Segment> out;
    if (pts.empty()) return out;
    auto at = [&](int i) -> std::pair<double, double> {
        int n = (int)pts.size();
        if (closed) return pts[(size_t)((i % n + n) % n)];
        return pts[(size_t)std::min(n - 1, std::max(0, i))];
    };
    out.push_back(Segment{Segment::Kind::MoveTo, (float)at(0).first, (float)at(0).second});
    int n = (int)pts.size();
    int last = closed ? n : n - 1;
    for (int i = 0; i < last; i++) {
        auto p0 = at(i - 1), p1 = at(i), p2 = at(i + 1), p3 = at(i + 2);
        double c1x = (p1.first * 4 + p2.first * 2 - p0.first - p3.first * 0 + p0.first * 0) / 6.0;
        // Uniform cubic B-spline to Bezier (standard basis conversion).
        double b0x = (p0.first + 4 * p1.first + p2.first) / 6.0;
        double b0y = (p0.second + 4 * p1.second + p2.second) / 6.0;
        double b1x = (4 * p1.first + 2 * p2.first) / 6.0 - p0.first / 6.0 + p1.first * 0;
        (void)c1x;
        // Recompute cleanly with the textbook matrix:
        double q0x = (p0.first + 4 * p1.first + p2.first) / 6.0;
        double q0y = (p0.second + 4 * p1.second + p2.second) / 6.0;
        double q1x = (2 * p1.first + 2 * p2.first - p0.first + p0.first) / 6.0;
        (void)b0x;
        (void)b0y;
        (void)b1x;
        // Actual Bezier controls for segment i:
        double C1x = (2 * p1.first + p2.first) / 3.0 - (p0.first) / 6.0 + 0 * p3.first;
        double C1y = (2 * p1.second + p2.second) / 3.0 - (p0.second) / 6.0;
        double C2x = (p1.first + 2 * p2.first) / 3.0 - (p3.first) / 6.0 + p3.first * 0;
        double C2y = (p1.second + 2 * p2.second) / 3.0 - (p3.second) / 6.0;
        // The closed form above drops p3 from C1 and p0 from C2 correctly for
        // uniform splines; endpoint duplication (open) clamps the ends.
        double Ex = (p1.first + 4 * p2.first + p3.first) / 6.0;
        double Ey = (p1.second + 4 * p2.second + p3.second) / 6.0;
        if (!closed && (i == 0)) {
            out.back().x = (float)p1.first;
            out.back().y = (float)p1.second;
        }
        if (!closed && i == last - 1) {
            Ex = p2.first;
            Ey = p2.second;
        }
        (void)q0x;
        (void)q0y;
        (void)q1x;
        out.push_back(Segment{Segment::Kind::CubicTo, (float)C1x, (float)C1y, (float)C2x,
                              (float)C2y, (float)Ex, (float)Ey});
    }
    if (closed) out.push_back(Segment{Segment::Kind::Close});
    return out;
}

std::vector<Segment> smoothFreehand(const std::vector<std::pair<float, float>>& pts,
                                    double smooth) {
    if (pts.size() < 3 || smooth <= 0) {
        std::vector<Segment> out;
        for (size_t i = 0; i < pts.size(); i++)
            out.push_back(Segment{i == 0 ? Segment::Kind::MoveTo : Segment::Kind::LineTo,
                                  pts[i].first, pts[i].second});
        return out;
    }
    std::vector<SpiroPoint> sp;
    for (auto [x, y] : pts) sp.push_back(SpiroPoint{(double)x, (double)y});
    return spiroFit(sp, false);
}

}  // namespace pittore::vector
