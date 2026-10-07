// Offset/fillet/simplify/stroke-family LPEs.
// Effect ids: lpe-offset, lpe-fillet-chamfer, path-simplify, lpe-powerstroke,
// lpe-taperstroke, lpe-dashed-stroke, lpe-dynastroke.
#include "engine/vector/lpe/lpe.h"

#include <cmath>

#include "engine/vector/path_ops.h"
#include "engine/vector/spiro.h"

namespace pittore::vector::lpe {
namespace {

struct OffsetEffect : Effect {
    OffsetEffect(const Params& p) { type = EffectType::Offset; params = p; }
    std::vector<Segment> apply(const std::vector<Segment>& src) const override {
        return offsetPath(src, params.getDouble("amount", 5.0));
    }
};

struct FilletEffect : Effect {
    FilletEffect(const Params& p) { type = EffectType::FilletChamfer; params = p; }
    std::vector<Segment> apply(const std::vector<Segment>& src) const override {
        return filletPath(src, params.getDouble("radius", 8.0),
                          params.getBool("chamfer", false));
    }
};

struct SimplifyEffect : Effect {
    SimplifyEffect(const Params& p) { type = EffectType::Simplify; params = p; }
    std::vector<Segment> apply(const std::vector<Segment>& src) const override {
        return simplifyPath(src, params.getDouble("threshold", 2.0), true);
    }
};

struct SpiroBsplineEffect : Effect {
    SpiroBsplineEffect(EffectType t, const Params& p) {
        type = t;
        params = p;
    }
    std::vector<Segment> apply(const std::vector<Segment>& src) const override {
        Path p = flattenSegments(src, 0.5f);
        std::vector<Segment> out;
        for (auto& sp : p.subpaths) {
            if (type == EffectType::Bspline) {
                std::vector<std::pair<double, double>> pts;
                for (auto [x, y] : sp) pts.emplace_back(x, y);
                auto r = bsplineFit(pts, false);
                out.insert(out.end(), r.begin(), r.end());
            } else {
                std::vector<SpiroPoint> pts;
                for (auto [x, y] : sp) pts.push_back(SpiroPoint{(double)x, (double)y});
                auto r = spiroFit(pts, false);
                out.insert(out.end(), r.begin(), r.end());
            }
        }
        return out.empty() ? src : out;
    }
};

struct StrokeFamilyEffect : Effect {
    StrokeFamilyEffect(EffectType t, const Params& p) {
        type = t;
        params = p;
    }
    std::vector<Segment> apply(const std::vector<Segment>& src) const override {
        if (type == EffectType::DashedStroke) {
            // Dashed rendering stays in the stroker; LPE output keeps the
            // spine so node editing still works (dash is a paint property).
            return src;
        }
        StrokeStyle st;
        st.width = (float)params.getDouble("width", 4.0);
        st.join = LineJoin::Round;
        st.cap = LineCap::Round;
        return strokeToSegments(src, st);
    }
};

}  // namespace

std::shared_ptr<Effect> makeOffsetEffect(const Params& p) {
    return std::make_shared<OffsetEffect>(p);
}
std::shared_ptr<Effect> makeFilletEffect(const Params& p) {
    return std::make_shared<FilletEffect>(p);
}
std::shared_ptr<Effect> makeSimplifyEffect(const Params& p) {
    return std::make_shared<SimplifyEffect>(p);
}
std::shared_ptr<Effect> makeStrokeEffects(EffectType t, const Params& p) {
    if (t == EffectType::Bspline || t == EffectType::Spiro)
        return std::make_shared<SpiroBsplineEffect>(t, p);
    return std::make_shared<StrokeFamilyEffect>(t, p);
}

}  // namespace pittore::vector::lpe
