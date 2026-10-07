#include "ui/persona/vector_profile.h"

#include <algorithm>
#include <cmath>

#include "engine/vector/vector_art.h"

namespace pittore::ui {

double maxProfileWidth(const pittore::vector::ArtPaint& paint) {
    if (!paint.hasStroke) return 0.0;
    if (!paint.hasProfile || paint.profile.empty()) return paint.strokeWidth;
    float peak = 0.0f;
    for (const auto& p : paint.profile) peak = std::max(peak, p.w);
    return std::max(0.01, paint.strokeWidth) * std::max(0.0f, peak);
}

pittore::vector::WidthProfile
widthProfileFrom(const pittore::vector::ArtPaint& paint) {
    pittore::vector::WidthProfile prof;
    if (!paint.hasProfile) return prof;
    for (const auto& p : paint.profile) {
        prof.pos.push_back(p.t);
        prof.scale.push_back(p.w);
    }
    return prof;
}

void setProfilePoint(pittore::vector::ArtPaint& paint, float t, float mult) {
    const float tc = std::clamp(t, 0.0f, 1.0f);
    const float mc = std::clamp(mult, 0.0f, 100.0f);
    if (!paint.hasProfile || paint.profile.empty()) {
        paint.hasProfile = true;
        paint.profile = {pittore::vector::ArtWidthPoint{0.0f, 1.0f},
                         pittore::vector::ArtWidthPoint{1.0f, 1.0f}};
    }
    auto& pts = paint.profile;
    for (auto& p : pts) {
        if (std::abs(p.t - tc) <= 0.02f) {
            p.w = mc;
            return;
        }
    }
    if (pts.size() >= 64) {
        // Cap total points: fold into the nearest instead of growing.
        std::size_t best = 0;
        float bestD = 1e30f;
        for (std::size_t i = 0; i < pts.size(); ++i) {
            const float d = std::abs(pts[i].t - tc);
            if (d < bestD) {
                bestD = d;
                best = i;
            }
        }
        pts[best].w = mc;
        return;
    }
    pts.push_back(pittore::vector::ArtWidthPoint{tc, mc});
    std::sort(pts.begin(), pts.end(), [](const auto& a, const auto& b) {
        return a.t < b.t;
    });
}

QPainterPath expandStrokePath(const pittore::vector::ArtNode& node) {
    QPainterPath out;
    const pittore::vector::ArtPaint& paint = node.paint;
    if (!paint.hasStroke || !paint.hasProfile || paint.profile.empty())
        return out;
    for (const auto& s : profiledStrokeSegments(node)) {
        switch (s.kind) {
            case pittore::vector::Segment::Kind::MoveTo:
                out.moveTo(s.x, s.y);
                break;
            case pittore::vector::Segment::Kind::LineTo:
                out.lineTo(s.x, s.y);
                break;
            case pittore::vector::Segment::Kind::CubicTo:
                out.cubicTo(s.c1x, s.c1y, s.c2x, s.c2y, s.x, s.y);
                break;
            case pittore::vector::Segment::Kind::Close:
                out.closeSubpath();
                break;
        }
    }
    out.setFillRule(Qt::WindingFill);
    return out;
}

}  // namespace pittore::ui
