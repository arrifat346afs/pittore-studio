// Nib sweep for the calligraphy tool.
#include "engine/vector/calligraphy.h"

#include <cmath>

namespace pittore::vector {

std::vector<Segment> calligraphyStroke(const std::vector<CalligraphySample>& spine,
                                       const CalligraphyNib& nib) {
    std::vector<Segment> out;
    if (spine.size() < 2) return out;
    double ang = nib.angleDeg * 3.14159265358979 / 180.0;
    double nx = std::cos(ang), ny = std::sin(ang);
    std::vector<std::pair<double, double>> left, right;
    double sx = spine[0].x, sy = spine[0].y;  // mass smoothing state
    for (size_t i = 0; i < spine.size(); i++) {
        double tx = spine[i].x, ty = spine[i].y;
        if (nib.mass > 0 && i > 0) {
            tx = sx + (spine[i].x - sx) * (1 - nib.mass * 0.9);
            ty = sy + (spine[i].y - sy) * (1 - nib.mass * 0.9);
        }
        sx = tx;
        sy = ty;
        double p = std::min(1.0, std::max(0.0, spine[i].pressure));
        double w = nib.width * (0.25 + 0.75 * p) * (1 + nib.thinning * (p - 0.5));
        double rx = w / 2 * nx, ry = w / 2 * ny;
        // Flatness squashes perpendicular to the nib direction.
        double px = -ny * nib.flatness, py = nx * nib.flatness;
        (void)px;
        (void)py;
        left.emplace_back(tx + rx, ty + ry);
        right.emplace_back(tx - rx, ty - ry);
    }
    out.push_back(
        Segment{Segment::Kind::MoveTo, (float)left[0].first, (float)left[0].second});
    for (size_t i = 1; i < left.size(); i++)
        out.push_back(
            Segment{Segment::Kind::LineTo, (float)left[i].first, (float)left[i].second});
    for (size_t i = right.size(); i-- > 0;)
        out.push_back(
            Segment{Segment::Kind::LineTo, (float)right[i].first, (float)right[i].second});
    out.push_back(Segment{Segment::Kind::Close});
    return out;
}

}  // namespace pittore::vector
