// Generate-family LPEs: tiling, mirror, rotate-copies, gears, interpolate,
// knot, curve-stitch, vonkoch, grids, pattern-along-path.
// Effect ids: lpe-tiling, lpe-mirror_symmetry, lpe-copy_rotate, lpe-gears,
// lpe-interpolate(_points), lpe-knot, lpe-curvestitch, lpe-constructgrid,
// lpe-patternalongpath.
#include "engine/vector/lpe/lpe.h"

#include <cmath>

namespace pittore::vector::lpe {
namespace {

struct GenerateEffect : Effect {
    GenerateEffect(EffectType t, const Params& p) {
        type = t;
        params = p;
    }
    std::vector<Segment> apply(const std::vector<Segment>& src) const override {
        if (src.empty()) return src;
        Path base = flattenSegments(src, 0.5f);
        std::vector<Segment> out;
        auto emitPoly = [&](const auto& pts, double dx,
                            double dy, double rotDeg, double sc) {
            double r = rotDeg * 3.14159265 / 180.0;
            double cr = std::cos(r) * sc, sr = std::sin(r) * sc;
            bool first = true;
            for (auto [x, y] : pts) {
                double nx = x * cr - y * sr + dx, ny = x * sr + y * cr + dy;
                out.push_back(Segment{first ? Segment::Kind::MoveTo : Segment::Kind::LineTo,
                                      (float)nx, (float)ny});
                first = false;
            }
        };
        int copies = (int)params.getLong("copies", 4);
        double gap = params.getDouble("gap", 20.0);
        switch (type) {
            case EffectType::Tiling: {
                int cols = (int)params.getLong("cols", 3);
                for (int k = 0; k < copies; k++)
                    for (auto& sp : base.subpaths)
                        emitPoly(sp, (k % cols) * gap, (k / cols) * gap, 0, 1);
                break;
            }
            case EffectType::MirrorSymmetry: {
                for (auto& sp : base.subpaths) emitPoly(sp, 0, 0, 0, 1);
                for (auto& sp : base.subpaths) {
                    std::vector<std::pair<double, double>> m;
                    for (auto [x, y] : sp) m.emplace_back(-x + 2 * gap, y);
                    emitPoly(m, 0, 0, 0, 1);
                }
                break;
            }
            case EffectType::CopyRotate: {
                double step = params.getDouble("angle", 360.0 / (copies > 0 ? copies : 1));
                for (int k = 0; k < copies; k++)
                    for (auto& sp : base.subpaths) emitPoly(sp, 0, 0, step * k, 1);
                break;
            }
            case EffectType::Gears: {
                int teeth = (int)params.getLong("teeth", 12);
                double rad = params.getDouble("radius", 40.0);
                for (int k = 0; k < teeth * 2; k++) {
                    double a = 2 * 3.14159 * k / (teeth * 2);
                    double rr = (k % 2 == 0) ? rad : rad * 0.85;
                    out.push_back(Segment{k == 0 ? Segment::Kind::MoveTo : Segment::Kind::LineTo,
                                          (float)(rr * std::cos(a)), (float)(rr * std::sin(a))});
                }
                out.push_back(Segment{Segment::Kind::Close});
                break;
            }
            case EffectType::Interpolate:
            case EffectType::InterpolatePoints: {
                // Blend spine toward its bbox center by `steps`.
                int steps = (int)params.getLong("steps", 3);
                for (int s = 0; s <= steps; s++) {
                    double t = (double)s / (steps + 1);
                    for (auto& sp : base.subpaths) {
                        bool first = true;
                        for (auto [x, y] : sp) {
                            double nx = x * (1 - t * 0.5), ny = y * (1 - t * 0.5);
                            out.push_back(Segment{first ? Segment::Kind::MoveTo
                                                       : Segment::Kind::LineTo,
                                                  (float)nx, (float)ny});
                            first = false;
                        }
                    }
                }
                break;
            }
            case EffectType::VonKoch: {
                // One Koch subdivision on each flattened edge.
                for (auto& sp : base.subpaths)
                    for (size_t i = 0; i + 1 < sp.size(); i++) {
                        double x0 = sp[i].first, y0 = sp[i].second;
                        double x1 = sp[i + 1].first, y1 = sp[i + 1].second;
                        double dx = (x1 - x0) / 3, dy = (y1 - y0) / 3;
                        double mx = x0 + dx * 1.5 - dy * 0.866, my = y0 + dy * 1.5 + dx * 0.866;
                        if (i == 0)
                            out.push_back(Segment{Segment::Kind::MoveTo, (float)x0, (float)y0});
                        out.push_back(
                            Segment{Segment::Kind::LineTo, (float)(x0 + dx), (float)(y0 + dy)});
                        out.push_back(
                            Segment{Segment::Kind::LineTo, (float)mx, (float)my});
                        out.push_back(Segment{Segment::Kind::LineTo, (float)(x0 + 2 * dx),
                                              (float)(y0 + 2 * dy)});
                        out.push_back(Segment{Segment::Kind::LineTo, (float)x1, (float)y1});
                    }
                break;
            }
            default: {
                // Knot, curve-stitch, grids, pattern-along-path, attach, fill
                // variants: passthrough here (their canvas knotholders live in
                // UI code); stack still records them for SVG round-trip.
                return src;
            }
        }
        return out.empty() ? src : out;
    }
};

}  // namespace

std::shared_ptr<Effect> makeGenerateEffect(EffectType t, const Params& p) {
    switch (t) {
        case EffectType::Tiling:
        case EffectType::MirrorSymmetry:
        case EffectType::CopyRotate:
        case EffectType::Gears:
        case EffectType::Interpolate:
        case EffectType::InterpolatePoints:
        case EffectType::VonKoch:
        case EffectType::Knot:
        case EffectType::CurveStitch:
        case EffectType::ConstructGrid:
        case EffectType::PatternAlongPath:
        case EffectType::AttachPath:
        case EffectType::FillBetweenMany:
        case EffectType::FillBetweenStrokes:
        case EffectType::CloneOriginal:
            return std::make_shared<GenerateEffect>(t, p);
        default: return nullptr;
    }
}

}  // namespace pittore::vector::lpe
