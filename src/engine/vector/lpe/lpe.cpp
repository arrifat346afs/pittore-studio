// LPE registry + dispatcher.
#include "engine/vector/lpe/lpe.h"

#include <cmath>

namespace pittore::vector::lpe {
namespace {

const EffectInfo kTable[] = {
    {EffectType::BendPath, "bendpath", "Bend path", Category::Distort},
    {EffectType::Gears, "gears", "Gears", Category::Generate},
    {EffectType::PatternAlongPath, "patternalongpath", "Pattern along path", Category::Generate},
    {EffectType::CurveStitch, "curvestitch", "Curve stitch", Category::Generate},
    {EffectType::VonKoch, "vonkoch", "VonKoch", Category::Generate},
    {EffectType::Knot, "knot", "Knot", Category::EditTools},
    {EffectType::ConstructGrid, "constructgrid", "Construct grid", Category::Generate, true},
    {EffectType::Spiro, "spiro", "Spiro spline", Category::EditTools},
    {EffectType::Envelope, "envelope", "Envelope deformation", Category::Distort},
    {EffectType::Interpolate, "interpolate", "Interpolate", Category::Generate},
    {EffectType::RoughHatches, "roughhatches", "Hatches (rough)", Category::Generate},
    {EffectType::Sketch, "sketch", "Sketch", Category::Distort},
    {EffectType::Ruler, "ruler", "Ruler", Category::Convert},
    {EffectType::Powerstroke, "powerstroke", "Power stroke", Category::EditTools},
    {EffectType::CloneOriginal, "clone_original", "Clone original", Category::Generate},
    {EffectType::Simplify, "simplify", "Simplify", Category::EditTools},
    {EffectType::Lattice2, "lattice2", "Lattice", Category::Distort},
    {EffectType::PerspectiveEnvelope, "perspective-envelope", "Perspective/envelope",
     Category::Distort},
    {EffectType::InterpolatePoints, "interpolate_points", "Interpolate points",
     Category::Generate},
    {EffectType::Transform2Pts, "transform_2pts", "Transform by 2 points", Category::EditTools},
    {EffectType::ShowHandles, "show_handles", "Show handles", Category::Experimental, true},
    {EffectType::Roughen, "roughen", "Roughen", Category::Distort},
    {EffectType::Bspline, "bspline", "B-spline", Category::EditTools},
    {EffectType::JoinType, "jointype", "Join type", Category::EditTools},
    {EffectType::TaperStroke, "taperstroke", "Taper stroke", Category::EditTools},
    {EffectType::MirrorSymmetry, "mirror_symmetry", "Mirror symmetry", Category::Generate},
    {EffectType::CopyRotate, "copy_rotate", "Rotate copies", Category::Generate},
    {EffectType::AttachPath, "attach_path", "Attach path", Category::Generate},
    {EffectType::FillBetweenMany, "fill_between_many", "Fill between many", Category::Generate},
    {EffectType::Ellipse5Pts, "ellipse_5pts", "Ellipse by 5 points", Category::Convert, true},
    {EffectType::BoundingBox, "bounding_box", "Bounding box", Category::Convert},
    {EffectType::MeasureSegments, "measure_segments", "Measure segments", Category::Convert},
    {EffectType::FilletChamfer, "fillet-chamfer", "Corners (fillet/chamfer)",
     Category::EditTools},
    {EffectType::Powerclip, "powerclip", "Power clip", Category::Convert},
    {EffectType::Powermask, "powermask", "Power mask", Category::Convert},
    {EffectType::Pts2Ellipse, "pts2ellipse", "Points to ellipse", Category::Convert, true},
    {EffectType::Offset, "offset", "Offset", Category::EditTools},
    {EffectType::DashedStroke, "dashed-stroke", "Dashed stroke", Category::EditTools},
    {EffectType::BoolOp, "bool", "Boolean operation", Category::EditTools},
    {EffectType::Slice, "slice", "Slice", Category::Convert},
    {EffectType::Tiling, "tiling", "Tiling / cloning", Category::Generate},
    {EffectType::AngleBisector, "angle_bisector", "Angle bisector", Category::Experimental, true},
    {EffectType::CircleWithRadius, "circle_with_radius", "Circle with radius",
     Category::Experimental, true},
    {EffectType::Circle3Pts, "circle_3pts", "Circle by 3 points", Category::Experimental,
     true},
    {EffectType::Extrude, "extrude", "Extrude", Category::Generate, true},
    {EffectType::LineSegment, "line_segment", "Line segment", Category::Experimental, true},
    {EffectType::Parallel, "parallel", "Parallel", Category::Experimental, true},
    {EffectType::PerpBisector, "perp_bisector", "Perpendicular bisector",
     Category::Experimental, true},
    {EffectType::TangentToCurve, "tangent_to_curve", "Tangent to curve",
     Category::Experimental, true},
    {EffectType::FillBetweenStrokes, "fill_between_strokes", "Fill between strokes",
     Category::Generate},
    {EffectType::DynaStroke, "dynastroke", "Dynamic stroke", Category::Experimental, true},
    {EffectType::Lattice, "lattice", "Lattice (legacy)", Category::Distort, true},
    {EffectType::PathLength, "path_length", "Path length", Category::Experimental, true},
    {EffectType::RecursiveSkeleton, "recursiveskeleton", "Recursive skeleton",
     Category::Experimental, true},
    {EffectType::TextLabel, "text_label", "Text label", Category::Experimental, true},
    {EffectType::EmbroideryStitch, "embrodery-stitch", "Embroidery stitch",
     Category::Experimental, true},
};

}  // namespace

const std::vector<EffectInfo>& allEffects() {
    static const std::vector<EffectInfo> v(std::begin(kTable), std::end(kTable));
    return v;
}

const EffectInfo* effectInfo(EffectType t) {
    for (auto& e : allEffects())
        if (e.type == t) return &e;
    return nullptr;
}

bool effectTypeFromKey(const std::string& key, EffectType& out) {
    for (auto& e : allEffects())
        if (key == e.key) {
            out = e.type;
            return true;
        }
    return false;
}

double Params::getDouble(const std::string& k, double dflt) const {
    auto it = values.find(k);
    if (it == values.end()) return dflt;
    try {
        return std::stod(it->second);
    } catch (...) {
        return dflt;
    }
}

long Params::getLong(const std::string& k, long dflt) const {
    auto it = values.find(k);
    if (it == values.end()) return dflt;
    try {
        return std::stol(it->second);
    } catch (...) {
        return dflt;
    }
}

bool Params::getBool(const std::string& k, bool dflt) const {
    auto it = values.find(k);
    if (it == values.end()) return dflt;
    return it->second == "true" || it->second == "1" || it->second == "yes";
}

std::string Params::getString(const std::string& k, const std::string& dflt) const {
    auto it = values.find(k);
    return it == values.end() ? dflt : it->second;
}

void Params::setDouble(const std::string& k, double v) { values[k] = std::to_string(v); }

std::string Effect::svgParams() const {
    std::string s;
    for (auto& [k, v] : params.values) {
        if (!s.empty()) s += ";";
        s += k + "=" + v;
    }
    return s;
}

std::vector<Segment> EffectStack::apply(const std::vector<Segment>& src) const {
    std::vector<Segment> cur = src;
    for (auto& e : items) cur = e->apply(cur);
    return cur;
}

// Factories live in the per-family translation units; declared here so the
// registry stays in one place while each family owns its code.
std::shared_ptr<Effect> makeOffsetEffect(const Params& p);
std::shared_ptr<Effect> makeFilletEffect(const Params& p);
std::shared_ptr<Effect> makeSimplifyEffect(const Params& p);
std::shared_ptr<Effect> makeStrokeEffects(EffectType t, const Params& p);
std::shared_ptr<Effect> makeDeformEffect(EffectType t, const Params& p);
std::shared_ptr<Effect> makeGenerateEffect(EffectType t, const Params& p);
std::shared_ptr<Effect> makeConvertEffect(EffectType t, const Params& p);

std::shared_ptr<Effect> makeEffect(EffectType type, const Params& params) {
    switch (type) {
        case EffectType::Offset: return makeOffsetEffect(params);
        case EffectType::FilletChamfer: return makeFilletEffect(params);
        case EffectType::Simplify: return makeSimplifyEffect(params);
        case EffectType::Powerstroke:
        case EffectType::TaperStroke:
        case EffectType::DashedStroke:
        case EffectType::DynaStroke: return makeStrokeEffects(type, params);
        case EffectType::BendPath:
        case EffectType::Envelope:
        case EffectType::Lattice:
        case EffectType::Lattice2:
        case EffectType::PerspectiveEnvelope:
        case EffectType::Roughen:
        case EffectType::Sketch:
        case EffectType::RoughHatches:
        case EffectType::Extrude:
        case EffectType::Ruler: return makeDeformEffect(type, params);
        default: break;
    }
    if (auto e = makeGenerateEffect(type, params)) return e;
    return makeConvertEffect(type, params);
}

std::shared_ptr<Effect> makeEffectByKey(const std::string& key, const Params& params) {
    EffectType t;
    if (!effectTypeFromKey(key, t)) return nullptr;
    return makeEffect(t, params);
}

}  // namespace pittore::vector::lpe
