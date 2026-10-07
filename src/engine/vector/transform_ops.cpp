// Align/distribute/arrange math.
#include "engine/vector/transform_ops.h"

#include <cmath>
#include <algorithm>

namespace pittore::vector {

std::vector<std::pair<double, double>> alignBoxes(std::vector<Bbox> boxes, AlignEdge edge,
                                                  double to) {
    std::vector<std::pair<double, double>> out;
    for (auto& b : boxes) {
        double dx = 0, dy = 0;
        switch (edge) {
            case AlignEdge::Left: dx = to - b.x0; break;
            case AlignEdge::HCenter: dx = to - b.cx(); break;
            case AlignEdge::Right: dx = to - b.x1; break;
            case AlignEdge::Top: dy = to - b.y0; break;
            case AlignEdge::VCenter: dy = to - b.cy(); break;
            case AlignEdge::Bottom: dy = to - b.y1; break;
        }
        out.emplace_back(dx, dy);
    }
    return out;
}

std::vector<std::pair<double, double>> distributeBoxes(std::vector<Bbox> boxes,
                                                       DistributeKind kind, double gap) {
    std::vector<std::pair<double, double>> out(boxes.size(), {0, 0});
    if (boxes.size() < 3) return out;
    // Sort by the distributed axis; keep original indices.
    std::vector<size_t> idx(boxes.size());
    for (size_t i = 0; i < idx.size(); i++) idx[i] = i;
    bool horiz = kind == DistributeKind::HGap || kind == DistributeKind::HCenter ||
                 kind == DistributeKind::HEdge;
    std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
        return horiz ? boxes[a].cx() < boxes[b].cx() : boxes[a].cy() < boxes[b].cy();
    });
    if (kind == DistributeKind::HGap || kind == DistributeKind::VGap) {
        double lo = horiz ? boxes[idx.front()].x0 : boxes[idx.front()].y0;
        double hi = horiz ? boxes[idx.back()].x1 : boxes[idx.back()].y1;
        double total = 0;
        for (auto i : idx) total += horiz ? boxes[i].w() : boxes[i].h();
        double g = gap > 0 ? gap : (hi - lo - total) / (idx.size() - 1);
        double cur = lo;
        // Keep first/last anchored; spread the middle.
        for (size_t k = 1; k + 1 < idx.size(); k++) {
            cur += (horiz ? boxes[idx[k - 1]].w() : boxes[idx[k - 1]].h()) + g;
            double want = cur;
            double have = horiz ? boxes[idx[k]].x0 : boxes[idx[k]].y0;
            if (horiz)
                out[idx[k]].first = want - have;
            else
                out[idx[k]].second = want - have;
            cur = want + (horiz ? boxes[idx[k]].w() : boxes[idx[k]].h());
        }
        return out;
    }
    // Centers/edges: even span between first and last.
    double lo = horiz ? boxes[idx.front()].cx() : boxes[idx.front()].cy();
    double hi = horiz ? boxes[idx.back()].cx() : boxes[idx.back()].cy();
    for (size_t k = 1; k + 1 < idx.size(); k++) {
        double want = lo + (hi - lo) * k / (idx.size() - 1);
        double have = horiz ? boxes[idx[k]].cx() : boxes[idx[k]].cy();
        if (horiz)
            out[idx[k]].first = want - have;
        else
            out[idx[k]].second = want - have;
    }
    return out;
}

void transformSpecToMatrix(const TransformSpec& spec, double m[6]) {
    if (spec.useMatrix) {
        for (int i = 0; i < 6; i++) m[i] = spec.matrix[i];
        return;
    }
    double r = spec.rotateDeg * 3.14159265358979 / 180.0;
    double kx = spec.skewXDeg * 3.14159265358979 / 180.0;
    double ky = spec.skewYDeg * 3.14159265358979 / 180.0;
    // M = T * R * K * S
    double a = spec.sx * std::cos(r), b = spec.sx * std::sin(r);
    double c = -spec.sy * std::sin(r), d = spec.sy * std::cos(r);
    double a2 = a + c * std::tan(kx), b2 = b + d * std::tan(kx);
    double c2 = c + a * std::tan(ky), d2 = d + b * std::tan(ky);
    m[0] = a2;
    m[1] = b2;
    m[2] = c2;
    m[3] = d2;
    m[4] = spec.dx;
    m[5] = spec.dy;
}

std::vector<std::pair<double, double>> arrangePositions(int n, const ArrangeSpec& spec) {
    std::vector<std::pair<double, double>> out;
    if (n <= 0) return out;
    const double pi = 3.14159265358979;
    if (spec.kind == ArrangeKind::Grid) {
        int cols = spec.cols > 0 ? spec.cols : 1;
        for (int i = 0; i < n; i++)
            out.emplace_back((i % cols) * spec.dx, (i / cols) * spec.dy);
    } else if (spec.kind == ArrangeKind::Circle || spec.kind == ArrangeKind::Polar) {
        for (int i = 0; i < n; i++) {
            double t = spec.startDeg + (spec.endDeg - spec.startDeg) * i / (n > 1 ? n - 1 : 1);
            double a = t * pi / 180.0;
            double r = spec.kind == ArrangeKind::Circle
                           ? spec.radius
                           : spec.radius * (n > 1 ? (double)i / (n - 1) : 1.0);
            out.emplace_back(r * std::cos(a), r * std::sin(a));
        }
    } else if (spec.kind == ArrangeKind::Honeycomb) {
        int cols = spec.cols > 0 ? spec.cols : 1;
        for (int i = 0; i < n; i++) {
            int c = i % cols, r = i / cols;
            out.emplace_back(c * spec.dx + (r % 2 ? spec.dx / 2 : 0), r * spec.dy * 0.866);
        }
    } else {
        for (int i = 0; i < n; i++) {
            double a = i * 2.39996;  // golden angle
            double r = spec.radius * std::sqrt((double)(i + 1) / (n > 0 ? n : 1));
            out.emplace_back(r * std::cos(a), r * std::sin(a));
        }
    }
    return out;
}

}  // namespace pittore::vector
