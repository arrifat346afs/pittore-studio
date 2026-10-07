#pragma once
// Live Path Effects: non-destructive path modification stack.
//
// Each effect is implemented in one lpe-*.cpp. An LPE wraps source Segments
// and produces output Segments on demand; the stack lives on the vector
// object so the editor re-runs it after every node drag.
// ParameterCorrrespondence is the ParamType subset the UI exposes
// (scalar/bool/enum/point); the rest round-trips as strings in `extra` so
// SVG save keeps unknown params.
//
// EffectType order and keys are fixed so .inx params and SVG
// `inkscape:path-effect` strings stay recognizable.
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "engine/vector/path.h"

namespace pittore::vector::lpe {

// Stable ids: order is fixed by EffectType's declaration order.
enum class EffectType {
    BendPath = 0,
    Gears,
    PatternAlongPath,
    CurveStitch,
    VonKoch,
    Knot,
    ConstructGrid,
    Spiro,
    Envelope,
    Interpolate,
    RoughHatches,
    Sketch,
    Ruler,
    Powerstroke,
    CloneOriginal,
    Simplify,
    Lattice2,
    PerspectiveEnvelope,
    InterpolatePoints,
    Transform2Pts,
    ShowHandles,
    Roughen,
    Bspline,
    JoinType,
    TaperStroke,
    MirrorSymmetry,
    CopyRotate,
    AttachPath,
    FillBetweenMany,
    Ellipse5Pts,
    BoundingBox,
    MeasureSegments,
    FilletChamfer,
    Powerclip,
    Powermask,
    Pts2Ellipse,
    Offset,
    DashedStroke,
    BoolOp,
    Slice,
    Tiling,
    AngleBisector,
    CircleWithRadius,
    Circle3Pts,
    Extrude,
    LineSegment,
    Parallel,
    PerpBisector,
    TangentToCurve,
    FillBetweenStrokes,
    DynaStroke,
    Lattice,
    PathLength,
    RecursiveSkeleton,
    TextLabel,
    EmbroideryStitch,
    Count_,
};

enum class Category { EditTools, Distort, Generate, Convert, Experimental };

struct EffectInfo {
    EffectType type;
    const char* key;    // "bendpath", "gears", ...
    const char* label;  // "Bend path", ...
    Category category;
    bool experimental = false;
};

const std::vector<EffectInfo>& allEffects();
const EffectInfo* effectInfo(EffectType t);
bool effectTypeFromKey(const std::string& key, EffectType& out);

// Parameter bag: typed accessors over string storage (SVG round-trip).
struct Params {
    std::map<std::string, std::string> values;
    double getDouble(const std::string& k, double dflt) const;
    long getLong(const std::string& k, long dflt) const;
    bool getBool(const std::string& k, bool dflt) const;
    std::string getString(const std::string& k, const std::string& dflt) const;
    void set(const std::string& k, const std::string& v) { values[k] = v; }
    void setDouble(const std::string& k, double v);
};

// One live effect instance (source in, result out).
struct Effect {
    EffectType type;
    Params params;
    virtual ~Effect() = default;
    virtual std::vector<Segment> apply(const std::vector<Segment>& src) const = 0;
    virtual std::string svgParams() const;  // `key=value;...` for save
};

// A stack: effects apply in order (index 0 first).
struct EffectStack {
    std::vector<std::shared_ptr<Effect>> items;
    bool empty() const { return items.empty(); }
    std::vector<Segment> apply(const std::vector<Segment>& src) const;
    void push(std::shared_ptr<Effect> e) { items.push_back(std::move(e)); }
    void clear() { items.clear(); }
};

// Factory: every registered type constructs (unknown keys -> null).
std::shared_ptr<Effect> makeEffect(EffectType type, const Params& params = {});
std::shared_ptr<Effect> makeEffectByKey(const std::string& key, const Params& params = {});

// Drag-derived params shared by the canvas gesture and the Path Effects
// panel (single implementation: same drag + amount always build the same
// effect). (x0,y0)→(x1,y1) is press→release in the target space (node space
// for hit art, document space for generative commits); amountPct is the
// bar's lpe_amount (0..500, 30 default).
Params dragParamsFor(EffectType type, double x0, double y0, double x1, double y1,
                     double amountPct);

}  // namespace pittore::vector::lpe
