// Deform-family LPEs: bend, envelope, lattice, perspective, extrude,
// roughen, sketch, hatches, ruler.
// Effect ids: lpe-bendpath, lpe-envelope, lpe-lattice2,
// lpe-perspective-envelope, lpe-roughen, lpe-sketch, lpe-rough-hatches,
// lpe-ruler, lpe-extrude.
#include "engine/vector/lpe/lpe.h"

#include <cmath>

namespace pittore::vector::lpe {
namespace {

std::vector<std::pair<double, double>> flattenAll(const std::vector<Segment>& src) {
    Path p = flattenSegments(src, 0.5f);
    std::vector<std::pair<double, double>> pts;
    for (auto& sp : p.subpaths)
        for (auto [x, y] : sp) pts.emplace_back(x, y);
    return pts;
}

void boundsOf(const std::vector<std::pair<double, double>>& pts, double& x0, double& y0,
              double& x1, double& y1) {
    if (pts.empty()) {
        x0 = y0 = x1 = y1 = 0;
        return;
    }
    x0 = x1 = pts[0].first;
    y0 = y1 = pts[0].second;
    for (auto [x, y] : pts) {
        x0 = std::min(x0, x);
        y0 = std::min(y0, y);
        x1 = std::max(x1, x);
        y1 = std::max(y1, y);
    }
}

struct DeformEffect : Effect {
    DeformEffect(EffectType t, const Params& p) {
        type = t;
        params = p;
    }
    std::vector<Segment> apply(const std::vector<Segment>& src) const override {
        auto pts = flattenAll(src);
        if (pts.empty()) return src;
        double x0, y0, x1, y1;
        boundsOf(pts, x0, y0, x1, y1);
        double w = std::max(1.0, x1 - x0), h = std::max(1.0, y1 - y0);
        std::vector<std::pair<double, double>> q;
        q.reserve(pts.size());
        double amt = params.getDouble("amount", 0.3);
        double freq = params.getDouble("freq", 3.0);
        std::uint32_t seed = (std::uint32_t)params.getLong("seed", 7);
        auto rnd = [&]() {
            seed = seed * 1664525u + 1013904223u;
            return (seed >> 8) / 16777216.0;
        };
        for (auto [x, y] : pts) {
            double u = (x - x0) / w, v = (y - y0) / h;
            double nx = x, ny = y;
            switch (type) {
                case EffectType::BendPath: {
                    // Quadratic bend around the horizontal midline.
                    double bend = params.getDouble("bend", amt * h);
                    nx = x;
                    ny = y + bend * 4 * u * (1 - u) * (params.getDouble("down", 1.0) > 0 ? 1 : -1);
                    break;
                }
                case EffectType::Envelope:
                case EffectType::PerspectiveEnvelope: {
                    double ex = params.getDouble("ex", amt), ey = params.getDouble("ey", amt);
                    nx = x + std::sin(v * 3.14159) * ex * w * (u - 0.5);
                    ny = y + std::sin(u * 3.14159) * ey * h * (v - 0.5);
                    break;
                }
                case EffectType::Lattice:
                case EffectType::Lattice2: {
                    nx = x + std::sin(v * freq * 6.2831) * amt * w * 0.1;
                    ny = y + std::sin(u * freq * 6.2831) * amt * h * 0.1;
                    break;
                }
                case EffectType::Roughen: {
                    double jx = (rnd() * 2 - 1) * amt * 4;
                    double jy = (rnd() * 2 - 1) * amt * 4;
                    nx = x + jx;
                    ny = y + jy;
                    break;
                }
                case EffectType::Sketch: {
                    double jx = std::sin(v * freq * 20 + seed) * amt * 3;
                    double jy = std::cos(u * freq * 20) * amt * 3;
                    nx = x + jx;
                    ny = y + jy;
                    break;
                }
                case EffectType::RoughHatches: {
                    // Hatching stays a deform here; the hatch LPE pass in UI
                    // code stamps HatchSpec strokes along the deformed spine.
                    nx = x + (rnd() - 0.5) * amt * 2;
                    ny = y + (rnd() - 0.5) * amt * 2;
                    break;
                }
                case EffectType::Extrude: {
                    double dx = params.getDouble("dx", 12), dy = params.getDouble("dy", 12);
                    nx = x + dx * u;
                    ny = y + dy * u;
                    break;
                }
                case EffectType::Ruler: {
                    // Ruler draws ticks; path unchanged (ticks are canvas UI).
                    break;
                }
                default: break;
            }
            q.emplace_back(nx, ny);
        }
        std::vector<Segment> out{Segment{Segment::Kind::MoveTo, (float)q[0].first,
                                         (float)q[0].second}};
        for (size_t i = 1; i < q.size(); i++)
            out.push_back(
                Segment{Segment::Kind::LineTo, (float)q[i].first, (float)q[i].second});
        return out;
    }
};

}  // namespace

std::shared_ptr<Effect> makeDeformEffect(EffectType t, const Params& p) {
    return std::make_shared<DeformEffect>(t, p);
}

}  // namespace pittore::vector::lpe
