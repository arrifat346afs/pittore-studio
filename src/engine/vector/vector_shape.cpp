#include "engine/vector/vector_shape.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <limits>

namespace pittore::vector {
namespace {

constexpr float kTau = 6.283185307179586f;
constexpr float kPi = 3.141592653589793f;
constexpr float kHalfPi = 1.5707963267948966f;

// Straight-alpha source-over of one coverage pixel, byte-exact with the way
// the rest of the import composites.
void over(std::uint8_t* px, const std::array<std::uint8_t, 4>& c,
          std::uint8_t cov) {
    const std::uint32_t sa = static_cast<std::uint32_t>(cov) * c[3] / 255u;
    const std::uint32_t da = px[3];
    const std::uint32_t outA = sa + da * (255u - sa) / 255u;
    if (outA == 0) return;
    for (int i = 0; i < 3; ++i) {
        px[i] = static_cast<std::uint8_t>(
            (static_cast<std::uint32_t>(c[i]) * sa +
             static_cast<std::uint32_t>(px[i]) * da * (255u - sa) / 255u) /
            outA);
    }
    px[3] = static_cast<std::uint8_t>(outA);
}

void layerIn(std::vector<std::uint8_t>& rgba,
             const std::vector<std::uint8_t>& cov,
             const std::array<std::uint8_t, 4>& color) {
    for (std::size_t i = 0; i < cov.size(); ++i) {
        if (cov[i] > 0) over(&rgba[i * 4], color, cov[i]);
    }
}

// Walk the monotonic wrap of `a` toward `b` by whole turns, the same
// normalization the arc fitter needs when the three points straddle zero.
void unwrapToward(float& a, float from) {
    while (a - from > kPi) a -= kTau;
    while (a - from < -kPi) a += kTau;
}

}  // namespace

IRect VectorPath::bounds() const {
    IRect out;
    for (const auto& sub : subpaths) {
        for (const auto& a : sub.anchors) {
            // A cubic stays inside the convex hull of its control points, so
            // folding the handle positions in bounds every curve; anchors
            // alone would clip a bulge that falls mid-segment.
            const float xs[3] = {a.px, a.px + a.hix, a.px + a.hox};
            const float ys[3] = {a.py, a.py + a.hiy, a.py + a.hoy};
            for (int i = 0; i < 3; ++i) {
                const IRect r{static_cast<int>(std::floor(xs[i])),
                              static_cast<int>(std::floor(ys[i])),
                              static_cast<int>(std::ceil(xs[i])) + 1,
                              static_cast<int>(std::ceil(ys[i])) + 1};
                out = out.unionWith(r);
            }
        }
    }
    return out;
}

std::array<std::uint8_t, 4> GradientFill::colorAt(float t) const {
    if (stops.empty()) return {0, 0, 0, 0};
    t = std::clamp(t, 0.0f, 1.0f);
    GradientStop prev = stops[0];
    for (const GradientStop& s : stops) {
        if (t <= s.pos) {
            const float span = s.pos - prev.pos;
            const float f =
                span <= FLT_EPSILON ? 1.0f : (t - prev.pos) / span;
            const auto mix = [f](std::uint8_t a, std::uint8_t b) {
                return static_cast<std::uint8_t>(static_cast<float>(a) +
                                                 (static_cast<float>(b) -
                                                  static_cast<float>(a)) *
                                                     f +
                                                 0.5f);
            };
            return {mix(prev.color[0], s.color[0]), mix(prev.color[1], s.color[1]),
                    mix(prev.color[2], s.color[2]), mix(prev.color[3], s.color[3])};
        }
        prev = s;
    }
    return stops.back().color;
}

std::optional<ShapeImage> rasterizeShape(const VectorShape& shape,
                                         const GradientFill* gradient) {
    const Path flat = flattenPath(shape.path, 0.25f);

    const int pad = shape.hasStroke
                        ? static_cast<int>(std::ceil(shape.strokeWidth / 2.0f + 1.0f))
                        : 1;
    IRect rect = shape.path.bounds();
    rect.left -= pad;
    rect.top -= pad;
    rect.right += pad;
    rect.bottom += pad;
    const int w = rect.width();
    const int h = rect.height();
    if (w <= 0 || h <= 0 ||
        static_cast<std::uint64_t>(w) * h > (1ull << 28))
        return std::nullopt;

    const FillRule rule = shape.evenOdd ? FillRule::EvenOdd : FillRule::NonZero;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * h * 4, 0);

    if (gradient) {
        const std::vector<std::uint8_t> cov = rasterize(flat, rect, rule);
        const double dx = gradient->endX - gradient->startX;
        const double dy = gradient->endY - gradient->startY;
        const double len2 = std::max(dx * dx + dy * dy, 1e-9);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const std::uint8_t a = cov[static_cast<std::size_t>(y) * w + x];
                if (a == 0) continue;
                const double px =
                    static_cast<double>(rect.left + x) + 0.5 - gradient->startX;
                const double py =
                    static_cast<double>(rect.top + y) + 0.5 - gradient->startY;
                const double t = gradient->radial
                                     ? std::sqrt((px * px + py * py) / len2)
                                     : (px * dx + py * dy) / len2;
                over(&rgba[(static_cast<std::size_t>(y) * w + x) * 4],
                     gradient->colorAt(static_cast<float>(t)), a);
            }
        }
    } else if (shape.fill[3] > 0) {
        layerIn(rgba, rasterize(flat, rect, rule), shape.fill);
    }

    if (shape.hasStroke) {
        StrokeStyle style(shape.strokeWidth);
        style.cap = LineCap::Round;
        style.join = LineJoin::Round;
        const Path stroked = strokePath(flat, style);
        layerIn(rgba, rasterize(stroked, rect, FillRule::NonZero), shape.stroke);
    }

    return ShapeImage{rect, std::move(rgba)};
}

std::optional<SubPath> subpathFromRecords(const std::vector<PathRecord>& records,
                                          bool closed) {
    std::vector<Anchor> anchors;
    // Incoming handle from an explicit `m1 == 2` record, consumed by the
    // next anchor (or the closing anchor). A plain bool tracks engagement:
    // std::optional's set/reset-in-a-loop pattern trips -Wmaybe-uninitialized
    // on guarded uses, while this is unambiguous to the analysis.
    std::pair<float, float> incoming{0.0f, 0.0f};
    bool haveIncoming = false;
    for (const PathRecord& r : records) {
        if (r.m0 == 0 && r.m1 == 1) {
            if (!anchors.empty()) {
                Anchor& prev = anchors.back();
                prev.hox = r.x - prev.px;
                prev.hoy = r.y - prev.py;
            }
        } else if (r.m0 == 0 && r.m1 == 2) {
            incoming = std::make_pair(r.x, r.y);
            haveIncoming = true;
        } else {
            Anchor a = Anchor::corner(r.x, r.y);
            if (haveIncoming) {
                a.hix = incoming.first - r.x;
                a.hiy = incoming.second - r.y;
                haveIncoming = false;
            }
            anchors.push_back(a);
        }
    }
    if (closed && haveIncoming && !anchors.empty()) {
        anchors.front().hix = incoming.first - anchors.front().px;
        anchors.front().hiy = incoming.second - anchors.front().py;
    }
    if (anchors.size() < 2) return std::nullopt;
    SubPath sub;
    sub.anchors = std::move(anchors);
    sub.closed = closed;
    return sub;
}

std::vector<Anchor> ellipseAnchors(float x0, float y0, float x1, float y1) {
    const float cx = (x0 + x1) * 0.5f;
    const float cy = (y0 + y1) * 0.5f;
    const float kx = kKappa * (x1 - x0) * 0.5f;
    const float ky = kKappa * (y1 - y0) * 0.5f;
    return {
        Anchor::smooth(cx, y0, kx, 0.0f),
        Anchor::smooth(x1, cy, 0.0f, ky),
        Anchor::smooth(cx, y1, -kx, 0.0f),
        Anchor::smooth(x0, cy, 0.0f, -ky),
    };
}

std::vector<Anchor> roundedRectAnchors(float x0, float y0, float x1, float y1,
                                       const std::array<float, 4>& radii) {
    return corneredRectAnchors(x0, y0, x1, y1, radii, {0, 0, 0, 0});
}

std::vector<Anchor> corneredRectAnchors(float x0, float y0, float x1, float y1,
                                        const std::array<float, 4>& radii,
                                        const std::array<std::uint16_t, 4>& types) {
    const float r[4] = {radii[0] < 0.25f ? 0.0f : radii[0],
                        radii[1] < 0.25f ? 0.0f : radii[1],
                        radii[2] < 0.25f ? 0.0f : radii[2],
                        radii[3] < 0.25f ? 0.0f : radii[3]};
    // Per corner: radius, corner point, and the unit vectors along the two
    // edges touching it.
    const float corner[4][2] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    const float entry[4][2] = {{0.0f, 1.0f}, {-1.0f, 0.0f}, {0.0f, -1.0f}, {1.0f, 0.0f}};
    const float exit[4][2] = {{1.0f, 0.0f}, {0.0f, 1.0f}, {-1.0f, 0.0f}, {0.0f, -1.0f}};

    std::vector<Anchor> out;
    out.reserve(8);
    for (int ci = 0; ci < 4; ++ci) {
        if (r[ci] == 0.0f) {
            out.push_back(Anchor::corner(corner[ci][0], corner[ci][1]));
            continue;
        }
        const float radius = r[ci];
        const float enx = corner[ci][0] + entry[ci][0] * radius;
        const float eny = corner[ci][1] + entry[ci][1] * radius;
        const float exx = corner[ci][0] + exit[ci][0] * radius;
        const float exy = corner[ci][1] + exit[ci][1] * radius;
        if (types[ci] == 1) {
            // Bevel: straight from one radius point across to the other.
            out.push_back(Anchor::corner(enx, eny));
            out.push_back(Anchor::corner(exx, exy));
            continue;
        }
        if (types[ci] == 2) {
            // Inverted: the curve bends around the corner point itself, so
            // each handle reaches toward the far side's radius point.
            const float k = kKappa * radius;
            Anchor a = Anchor::corner(enx, eny);
            a.hox = exit[ci][0] * k;
            a.hoy = exit[ci][1] * k;
            out.push_back(a);
            Anchor b = Anchor::corner(exx, exy);
            b.hix = entry[ci][0] * k;
            b.hiy = entry[ci][1] * k;
            out.push_back(b);
            continue;
        }
        if (types[ci] == 3) {
            // Notch: step in to a point tucked inside the corner, then step
            // back out to the far radius point.
            out.push_back(Anchor::corner(enx, eny));
            out.push_back(Anchor::corner(
                corner[ci][0] + (entry[ci][0] + exit[ci][0]) * radius,
                corner[ci][1] + (entry[ci][1] + exit[ci][1]) * radius));
            out.push_back(Anchor::corner(exx, exy));
            continue;
        }
        // The straight edges either side keep zero handles; the arc between
        // the two anchors bends toward the corner point.
        const float k = kKappa * radius;
        Anchor a = Anchor::corner(enx, eny);
        a.hox = -entry[ci][0] * k;
        a.hoy = -entry[ci][1] * k;
        out.push_back(a);
        Anchor b = Anchor::corner(exx, exy);
        b.hix = -exit[ci][0] * k;
        b.hiy = -exit[ci][1] * k;
        out.push_back(b);
    }
    return out;
}

std::vector<Anchor> arcAnchors(float cx, float cy, float rx, float ry, float t0,
                               float t1) {
    int n = static_cast<int>(std::ceil(std::abs(t1 - t0) / kHalfPi));
    if (n < 1) n = 1;
    const float dt = (t1 - t0) / static_cast<float>(n);
    const float k = 4.0f / 3.0f * std::tan(dt / 4.0f);
    std::vector<Anchor> out;
    out.reserve(static_cast<std::size_t>(n) + 1);
    for (int i = 0; i <= n; ++i) {
        const float t = t0 + dt * static_cast<float>(i);
        const float px = cx + rx * std::cos(t);
        const float py = cy + ry * std::sin(t);
        const float dx = -rx * std::sin(t) * k;
        const float dy = ry * std::cos(t) * k;
        Anchor a = Anchor::corner(px, py);
        if (i > 0) {
            a.hix = -dx;
            a.hiy = -dy;
        }
        if (i < n) {
            a.hox = dx;
            a.hoy = dy;
        }
        out.push_back(a);
    }
    return out;
}

Anchor unitAnchor(float ux, float uy, float x0, float y0, float x1, float y1) {
    return Anchor::corner((x0 + x1) * 0.5f + ux * (x1 - x0) * 0.5f,
                          (y0 + y1) * 0.5f + uy * (y1 - y0) * 0.5f);
}

std::vector<Anchor> squareStarAnchors(std::uint32_t sides, float cut, float x0,
                                      float y0, float x1, float y1) {
    const float step = kTau / static_cast<float>(sides);
    const float tip = std::cos(kPi / static_cast<float>(sides));
    const float hw = cut * std::sin(kPi / static_cast<float>(sides));
    std::vector<Anchor> out;
    out.reserve(static_cast<std::size_t>(sides) * 3);
    for (std::uint32_t k = 0; k < sides; ++k) {
        const float ang = kHalfPi + step * static_cast<float>(k);
        const float ux = std::cos(ang), uy = std::sin(ang);
        const float px = -uy, py = ux;  // toward the next arm
        out.push_back(unitAnchor(ux * tip - px * hw, uy * tip - py * hw, x0, y0, x1, y1));
        out.push_back(unitAnchor(ux * tip + px * hw, uy * tip + py * hw, x0, y0, x1, y1));
        const float na = ang + step * 0.5f;
        out.push_back(unitAnchor(std::cos(na) * cut, std::sin(na) * cut, x0, y0, x1, y1));
    }
    return out;
}

std::optional<std::pair<std::pair<float, float>, float>> circleThrough(
    std::pair<float, float> p0, std::pair<float, float> p1,
    std::pair<float, float> p2) {
    const float d = 2.0f * (p0.first * (p1.second - p2.second) +
                            p1.first * (p2.second - p0.second) +
                            p2.first * (p0.second - p1.second));
    if (std::abs(d) < 1e-9f) return std::nullopt;
    const auto sq = [](std::pair<float, float> p) {
        return p.first * p.first + p.second * p.second;
    };
    const float cx = (sq(p0) * (p1.second - p2.second) +
                      sq(p1) * (p2.second - p0.second) +
                      sq(p2) * (p0.second - p1.second)) /
                     d;
    const float cy = (sq(p0) * (p2.first - p1.first) +
                      sq(p1) * (p0.first - p2.first) +
                      sq(p2) * (p1.first - p0.first)) /
                     d;
    return std::make_pair(std::make_pair(cx, cy),
                          std::hypot(p0.first - cx, p0.second - cy));
}

std::vector<Anchor> cloudAnchors(std::uint32_t bubbles, float meet, float x0,
                                 float y0, float x1, float y1) {
    const float step = kTau / static_cast<float>(bubbles);
    // Anchors alternate meet points and bubble peaks in unit space; handles
    // come from the exact circle through (meet, peak, meet).
    std::vector<Anchor> out;
    out.reserve(static_cast<std::size_t>(bubbles) * 2);
    for (std::uint32_t i = 0; i < bubbles * 2; ++i) {
        const float ang = -kHalfPi + step * 0.5f * static_cast<float>(i) - step * 0.5f;
        const float r = (i % 2 == 0) ? meet : 1.0f;
        out.push_back(Anchor::corner(std::cos(ang) * r, std::sin(ang) * r));
    }
    const std::size_t n = out.size();
    for (std::uint32_t k = 0; k < bubbles; ++k) {
        const auto p0 = std::make_pair(out[2 * k].px, out[2 * k].py);
        const auto p1 = std::make_pair(out[2 * k + 1].px, out[2 * k + 1].py);
        const auto p2 = std::make_pair(out[(2 * k + 2) % n].px, out[(2 * k + 2) % n].py);
        const auto circle = circleThrough(p0, p1, p2);
        if (!circle) continue;
        const auto& c = circle->first;
        const float r = circle->second;
        const auto ang = [&c](std::pair<float, float> p) {
            return std::atan2(p.second - c.second, p.first - c.first);
        };
        const float a0 = ang(p0);
        float a1 = ang(p1);
        float a2 = ang(p2);
        unwrapToward(a1, a0);
        unwrapToward(a2, a1);
        const auto tangent = [&](float a, float kk) {
            return std::make_pair(-std::sin(a) * kk * r, std::cos(a) * kk * r);
        };
        const float k01 = (4.0f / 3.0f) * std::tan((a1 - a0) / 4.0f);
        const float k12 = (4.0f / 3.0f) * std::tan((a2 - a1) / 4.0f);
        const auto t0 = tangent(a0, k01);
        const auto t1 = tangent(a1, k01);
        const auto t2 = tangent(a1, k12);
        const auto t3 = tangent(a2, k12);
        out[2 * k].hox = t0.first;
        out[2 * k].hoy = t0.second;
        out[2 * k + 1].hix = -t1.first;
        out[2 * k + 1].hiy = -t1.second;
        out[2 * k + 1].hox = t2.first;
        out[2 * k + 1].hoy = t2.second;
        out[(2 * k + 2) % n].hix = -t3.first;
        out[(2 * k + 2) % n].hiy = -t3.second;
    }
    for (Anchor& a : out) {
        const Anchor mapped = unitAnchor(a.px, a.py, x0, y0, x1, y1);
        const float sx = (x1 - x0) * 0.5f;
        const float sy = (y1 - y0) * 0.5f;
        a.px = mapped.px;
        a.py = mapped.py;
        a.hix *= sx;
        a.hiy *= sy;
        a.hox *= sx;
        a.hoy *= sy;
    }
    return out;
}

std::vector<Anchor> heartAnchors(float x0, float y0, float x1, float y1,
                                 float spread) {
    const float w = x1 - x0, h = y1 - y0;
    const auto p = [&](float ux, float uy) {
        return std::make_pair(x0 + ux * w, y0 + uy * h);
    };
    const auto v = [&](float ux, float uy) {
        return std::make_pair(ux * w, uy * h);
    };
    const float notch = std::clamp(spread * 0.82f, 0.0f, 0.6f);
    const auto a = [](std::pair<float, float> pt, std::pair<float, float> hin,
                      std::pair<float, float> hout) {
        Anchor an = Anchor::corner(pt.first, pt.second);
        an.hix = hin.first;
        an.hiy = hin.second;
        an.hox = hout.first;
        an.hoy = hout.second;
        return an;
    };
    return {
        a(p(0.5f, 1.0f), v(0.26f, -0.14f), v(-0.26f, -0.14f)),  // bottom tip
        a(p(0.0f, 0.35f), v(0.0f, 0.25f), v(0.0f, -0.20f)),     // left flank
        a(p(0.25f, 0.0f), v(-0.14f, 0.0f), v(0.14f, 0.0f)),     // left lobe top
        a(p(0.5f, notch), v(-0.045f, -0.10f), v(0.045f, -0.10f)),  // notch
        a(p(0.75f, 0.0f), v(-0.14f, 0.0f), v(0.14f, 0.0f)),     // right lobe top
        a(p(1.0f, 0.35f), v(0.0f, -0.20f), v(0.0f, 0.25f)),     // right flank
    };
}

std::vector<Anchor> bowArcUnit(float bow, bool downward) {
    const float s = std::min(std::abs(bow) * 0.5f, 0.5f);
    if (s < 0.005f) {
        std::vector<Anchor> pts = {Anchor::corner(0.5f, 0.0f),
                                   Anchor::corner(0.5f, 1.0f)};
        if (!downward) std::reverse(pts.begin(), pts.end());
        return pts;
    }
    const float r = (s * s + 0.25f) / (2.0f * s);
    // The arc bows clear of the chord, so its centre lies on the far side.
    const float cxu = 0.5f + (r - s);
    const float phi = std::atan2(0.5f, r - s);
    const float t0 = downward ? (kPi + phi) : (kPi - phi);
    const float t1 = downward ? (kPi - phi) : (kPi + phi);
    std::vector<Anchor> a = arcAnchors(cxu, 0.5f, r, r, t0, t1);
    if (bow > 0.0f) {
        for (Anchor& an : a) {
            an.px = 1.0f - an.px;
            an.hix = -an.hix;
            an.hox = -an.hox;
        }
    }
    return a;
}

Path flattenPath(const VectorPath& path, float tolerance) {
    PathBuilder builder;
    for (const SubPath& sub : path.subpaths) {
        if (sub.anchors.empty()) continue;
        const Anchor& first = sub.anchors.front();
        builder.moveTo(first.px, first.py);
        const std::size_t n = sub.anchors.size();
        for (std::size_t i = 1; i <= n; ++i) {
            const Anchor& from = sub.anchors[i - 1];
            const Anchor& to = sub.anchors[i % n];
            if (i == n && !sub.closed) break;
            builder.cubicTo(from.px + from.hox, from.py + from.hoy,
                            to.px + to.hix, to.py + to.hiy, to.px, to.py);
        }
        if (sub.closed) builder.close();
    }
    return builder.build(tolerance);
}

}  // namespace pittore::vector
