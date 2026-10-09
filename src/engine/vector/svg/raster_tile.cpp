// Scan fill. Curves split flat, arcs as lines for now.
#include "engine/vector/svg/raster_tile.h"

#include <cmath>

#include "engine/vector/svg/clip_path.h"
#include "engine/vector/svg/poly_close.h"
#include "engine/vector/svg/quad_split.h"
#include "engine/vector/svg/rect_corners.h"
#include "engine/vector/svg/render_item.h"
#include "engine/vector/svg/smooth_hint.h"
#include "engine/vector/svg/thread_pool.h"

namespace pittore::svg {
namespace {

// Cubic point at t.
void cubicPt(double x0, double y0, double x1, double y1, double x2, double y2,
             double x3, double y3, double t, double& ox, double& oy) {
    const double u = 1 - t;
    ox = u * u * u * x0 + 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t * x3;
    oy = u * u * u * y0 + 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t * y3;
}

// Segments to rings. Tracks current point and prior controls.
std::vector<std::vector<std::pair<double, double>>> polygonize(
    const std::vector<PathSeg>& segs) {
    std::vector<std::vector<std::pair<double, double>>> rings;
    std::vector<std::pair<double, double>> cur;
    double px = 0, py = 0, sx = 0, sy = 0;
    double lastCx = 0, lastCy = 0, lastQx = 0, lastQy = 0;
    bool hasCubic = false, hasQuad = false;
    auto flush = [&]() {
        if (cur.size() >= 3) {
            rings.push_back(cur);
        }
        cur.clear();
    };
    for (const auto& s : segs) {
        if (s.type == SegType::Move) {
            flush();
            px = s.rel ? px + s.v[0] : s.v[0];
            py = s.rel ? py + s.v[1] : s.v[1];
            sx = px;
            sy = py;
            cur.emplace_back(px, py);
            hasCubic = hasQuad = false;
        } else if (s.type == SegType::Line || s.type == SegType::H ||
                   s.type == SegType::V) {
            double nx = px, ny = py;
            if (s.type == SegType::Line) {
                nx = s.rel ? px + s.v[0] : s.v[0];
                ny = s.rel ? py + s.v[1] : s.v[1];
            } else if (s.type == SegType::H) {
                nx = s.rel ? px + s.v[0] : s.v[0];
            } else {
                ny = s.rel ? py + s.v[0] : s.v[0];
            }
            cur.emplace_back(nx, ny);
            px = nx;
            py = ny;
            hasCubic = hasQuad = false;
        } else if (s.type == SegType::Cubic) {
            double x1 = s.rel ? px + s.v[0] : s.v[0];
            double y1 = s.rel ? py + s.v[1] : s.v[1];
            double x2 = s.rel ? px + s.v[2] : s.v[2];
            double y2 = s.rel ? py + s.v[3] : s.v[3];
            double x3 = s.rel ? px + s.v[4] : s.v[4];
            double y3 = s.rel ? py + s.v[5] : s.v[5];
            for (int k = 1; k <= 16; ++k) {
                double ox = 0, oy = 0;
                cubicPt(px, py, x1, y1, x2, y2, x3, y3, k / 16.0, ox, oy);
                cur.emplace_back(ox, oy);
            }
            lastCx = x2;
            lastCy = y2;
            hasCubic = true;
            hasQuad = false;
            px = x3;
            py = y3;
        } else if (s.type == SegType::SmoothCubic) {
            double x1 = px, y1 = py;
            if (hasCubic) {
                reflectPoint(lastCx, lastCy, px, py, x1, y1);
            }
            double x2 = s.rel ? px + s.v[0] : s.v[0];
            double y2 = s.rel ? py + s.v[1] : s.v[1];
            double x3 = s.rel ? px + s.v[2] : s.v[2];
            double y3 = s.rel ? py + s.v[3] : s.v[3];
            for (int k = 1; k <= 16; ++k) {
                double ox = 0, oy = 0;
                cubicPt(px, py, x1, y1, x2, y2, x3, y3, k / 16.0, ox, oy);
                cur.emplace_back(ox, oy);
            }
            lastCx = x2;
            lastCy = y2;
            hasCubic = true;
            hasQuad = false;
            px = x3;
            py = y3;
        } else if (s.type == SegType::Quad) {
            double qx = s.rel ? px + s.v[0] : s.v[0];
            double qy = s.rel ? py + s.v[1] : s.v[1];
            double x3 = s.rel ? px + s.v[2] : s.v[2];
            double y3 = s.rel ? py + s.v[3] : s.v[3];
            double c1x = 0, c1y = 0, c2x = 0, c2y = 0;
            quadToCubic(px, py, qx, qy, x3, y3, c1x, c1y, c2x, c2y);
            for (int k = 1; k <= 12; ++k) {
                double ox = 0, oy = 0;
                cubicPt(px, py, c1x, c1y, c2x, c2y, x3, y3, k / 12.0, ox, oy);
                cur.emplace_back(ox, oy);
            }
            lastQx = qx;
            lastQy = qy;
            hasQuad = true;
            hasCubic = false;
            px = x3;
            py = y3;
        } else if (s.type == SegType::SmoothQuad) {
            double qx = px, qy = py;
            if (hasQuad) {
                reflectPoint(lastQx, lastQy, px, py, qx, qy);
            }
            double x3 = s.rel ? px + s.v[0] : s.v[0];
            double y3 = s.rel ? py + s.v[1] : s.v[1];
            double c1x = 0, c1y = 0, c2x = 0, c2y = 0;
            quadToCubic(px, py, qx, qy, x3, y3, c1x, c1y, c2x, c2y);
            for (int k = 1; k <= 12; ++k) {
                double ox = 0, oy = 0;
                cubicPt(px, py, c1x, c1y, c2x, c2y, x3, y3, k / 12.0, ox, oy);
                cur.emplace_back(ox, oy);
            }
            lastQx = qx;
            lastQy = qy;
            hasQuad = true;
            hasCubic = false;
            px = x3;
            py = y3;
        } else if (s.type == SegType::Arc) {
            // Gap: arc curve kept as endpoint line.
            const double nx = s.rel ? px + s.v[5] : s.v[5];
            const double ny = s.rel ? py + s.v[6] : s.v[6];
            cur.emplace_back(nx, ny);
            px = nx;
            py = ny;
            hasCubic = hasQuad = false;
        } else {
            if (!cur.empty()) {
                cur.push_back({sx, sy});
            }
            flush();
            px = sx;
            py = sy;
            hasCubic = hasQuad = false;
        }
    }
    flush();
    return rings;
}

// World point of a local point.
void toWorld(const Affine& m, double x, double y, double& ox, double& oy) {
    ox = m.a * x + m.c * y + m.e;
    oy = m.b * x + m.d * y + m.f;
}

// Coverage of one item at pixel center.
bool covers(const RenderItem& it, double x, double y) {
    double lx = x, ly = y;
    // To local: skip inverse when identity-ish is not needed; use full
    // inverse only for rect/circle/ellipse fast paths below via world map
    // by testing world-mapped shape instead. Here invert once per item is
    // done by the caller; this takes local coords.
    (void)it;
    (void)lx;
    (void)ly;
    return false;
}

}  // namespace

void paintItemFlat(const RenderItem& it, FeImg& buf) {
    if (it.style.fill.none && !it.style.fill.hasRef) {
        // Fill defaults to black only at scene build; ref fills skip here.
        if (it.style.fill.none) {
            return;
        }
    }
    if (it.style.fill.hasRef) {
        return;  // Gap: gradient fills need a paint store.
    }
    const float a =
        it.style.fill.a * (float)it.style.fillOpacity * (float)it.style.opacity;
    if (a <= 0 || buf.w <= 0 || buf.h <= 0) {
        return;
    }
    const float fr = it.style.fill.r, fg = it.style.fill.g, fb = it.style.fill.b;
    auto pool = getPool();
    if (it.kind == ItemKind::Rect) {
        double rx = it.rx, ry = it.ry;
        clampRadii(it.w, it.h, rx, ry);
        pool->dispatch_threshold(
            buf.h, buf.w * buf.h > 2048, [&](int yy, int) {
                for (int xx = 0; xx < buf.w; ++xx) {
                    double wx = xx + 0.5, wy = yy + 0.5;
                    // To local.
                    double det = it.world.a * it.world.d - it.world.b * it.world.c;
                    double lx = wx, ly = wy;
                    if (det != 0) {
                        lx = (it.world.d * (wx - it.world.e) -
                              it.world.c * (wy - it.world.f)) /
                             det;
                        ly = (-it.world.b * (wx - it.world.e) +
                              it.world.a * (wy - it.world.f)) /
                             det;
                    }
                    bool hit = lx >= it.x && lx < it.x + it.w && ly >= it.y &&
                               ly < it.y + it.h;
                    if (hit && (rx > 0 || ry > 0)) {
                        const double cx =
                            lx < it.x + rx ? it.x + rx : lx > it.x + it.w - rx
                                                         ? it.x + it.w - rx
                                                         : lx;
                        const double cy =
                            ly < it.y + ry ? it.y + ry : ly > it.y + it.h - ry
                                                         ? it.y + it.h - ry
                                                         : ly;
                        const double dx = (lx - cx) / (rx > 0 ? rx : 1);
                        const double dy = (ly - cy) / (ry > 0 ? ry : 1);
                        const bool corner = lx < it.x + rx || lx > it.x + it.w - rx;
                        const bool edge = ly < it.y + ry || ly > it.y + it.h - ry;
                        if (corner && edge && dx * dx + dy * dy > 1) {
                            hit = false;
                        }
                    }
                    if (!hit) {
                        continue;
                    }
                    float* p = &buf.px[(size_t)((yy * buf.w + xx) * 4)];
                    const float da = a + p[3] * (1 - a);
                    p[0] = fr * a + p[0] * (1 - a);
                    p[1] = fg * a + p[1] * (1 - a);
                    p[2] = fb * a + p[2] * (1 - a);
                    p[3] = da;
                }
            });
        return;
    }
    if (it.kind == ItemKind::Circle || it.kind == ItemKind::Ellipse) {
        const double crx = it.kind == ItemKind::Circle ? it.r : it.rx;
        const double cry = it.kind == ItemKind::Circle ? it.r : it.ry;
        if (crx <= 0 || cry <= 0) {
            return;
        }
        pool->dispatch_threshold(
            buf.h, buf.w * buf.h > 2048, [&](int yy, int) {
                for (int xx = 0; xx < buf.w; ++xx) {
                    double wx = xx + 0.5, wy = yy + 0.5;
                    double det = it.world.a * it.world.d - it.world.b * it.world.c;
                    double lx = wx, ly = wy;
                    if (det != 0) {
                        lx = (it.world.d * (wx - it.world.e) -
                              it.world.c * (wy - it.world.f)) /
                             det;
                        ly = (-it.world.b * (wx - it.world.e) +
                              it.world.a * (wy - it.world.f)) /
                             det;
                    }
                    const double dx = (lx - it.cx) / crx;
                    const double dy = (ly - it.cy) / cry;
                    if (dx * dx + dy * dy > 1) {
                        continue;
                    }
                    float* p = &buf.px[(size_t)((yy * buf.w + xx) * 4)];
                    const float da = a + p[3] * (1 - a);
                    p[0] = fr * a + p[0] * (1 - a);
                    p[1] = fg * a + p[1] * (1 - a);
                    p[2] = fb * a + p[2] * (1 - a);
                    p[3] = da;
                }
            });
        return;
    }
    // Poly and path go through world-mapped rings.
    std::vector<std::vector<std::pair<double, double>>> rings;
    if (it.kind == ItemKind::Poly) {
        rings.push_back(it.points);
        if (it.closePoly) {
            closeRing(rings.back());
        }
    } else if (it.kind == ItemKind::Path) {
        rings = polygonize(it.segs);
    } else {
        return;  // Gap: line, text, image, use need stroke/glyph passes.
    }
    for (auto& r : rings) {
        for (auto& p : r) {
            double ox = 0, oy = 0;
            toWorld(it.world, p.first, p.second, ox, oy);
            p.first = ox;
            p.second = oy;
        }
    }
    pool->dispatch_threshold(
        buf.h, buf.w * buf.h > 2048, [&](int yy, int) {
            for (int xx = 0; xx < buf.w; ++xx) {
                const double wx = xx + 0.5, wy = yy + 0.5;
                bool hit = false;
                for (const auto& r : rings) {
                    if (pointInPoly(r, wx, wy, false)) {
                        hit = true;
                        break;
                    }
                }
                if (!hit) {
                    continue;
                }
                float* p = &buf.px[(size_t)((yy * buf.w + xx) * 4)];
                const float da = a + p[3] * (1 - a);
                p[0] = fr * a + p[0] * (1 - a);
                p[1] = fg * a + p[1] * (1 - a);
                p[2] = fb * a + p[2] * (1 - a);
                p[3] = da;
            }
        });
    (void)covers;
}

}  // namespace pittore::svg
