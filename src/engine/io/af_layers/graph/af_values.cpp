#include "engine/io/af_layers.h"
#include "engine/io/af.h"
#include "engine/render/layer_style.h"
#include "engine/text/text_engine.h"
#include "engine/vector/vector_shape.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <zlib.h>
#ifdef PITTORE_AF
#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>
#endif
#ifdef PITTORE_WEBP
#include "engine/io/webp.h"
#endif
#ifdef PITTORE_JPEG
#include <csetjmp>
#include <cstdio>
#include <jpeglib.h>
#endif
#include "engine/io/af_layers/container/af_archive.h"
#include "engine/io/af_layers/geom/af_geom.h"
#include "engine/io/af_layers/geom/af_homography.h"
#include "engine/io/af_layers/graph/af_graph.h"
#include "engine/io/af_layers/graph/af_graph_parser.h"
#include "engine/io/af_layers/image/af_image.h"
#include "engine/io/af_layers/filter/af_blur.h"
#include "engine/io/af_layers/filter/af_distort.h"
#include "engine/io/af_layers/filter/af_fx.h"
#include "engine/io/af_layers/filter/af_live.h"
#include "engine/io/af_layers/vector/af_shapes.h"
#include "engine/io/af_layers/walker/af_walker.h"

namespace pittore::io {
namespace af_detail {


// ---------------------------------------------------------------------------
// Field accessors
// ---------------------------------------------------------------------------

std::optional<std::uint16_t> enumOf(const Graph& g, const Node* n, const char* name) {
    const Value* v = g.field(n, name);
    if (v && v->k == Value::K::Enum) return static_cast<std::uint16_t>(v->u);
    return std::nullopt;
}

std::optional<std::pair<std::uint16_t, std::uint16_t>> enumVerOf(const Graph& g, const Node* n,
                                                                const char* name) {
    const Value* v = g.field(n, name);
    if (v && v->k == Value::K::Enum)
        return std::make_pair(static_cast<std::uint16_t>(v->u),
                              static_cast<std::uint16_t>(v->ver));
    return std::nullopt;
}

std::int32_t i32Of(const Graph& g, const Node* n, const char* name, std::int32_t dflt) {
    const Value* v = g.field(n, name);
    if (!v) return dflt;
    if (v->k == Value::K::I32) return static_cast<std::int32_t>(v->u);
    if (v->k == Value::K::U32) return static_cast<std::int32_t>(v->u);
    return dflt;
}

std::optional<float> f32Of(const Graph& g, const Node* n, const char* name) {
    const Value* v = g.field(n, name);
    if (v && v->k == Value::K::F32) return static_cast<float>(v->f);
    return std::nullopt;
}

std::optional<bool> boolOf(const Graph& g, const Node* n, const char* name) {
    const Value* v = g.field(n, name);
    if (v && v->k == Value::K::Bool) return v->u != 0;
    return std::nullopt;
}

std::string strOf(const Graph& g, const Node* n, const char* name) {
    const Value* v = g.field(n, name);
    if (v && v->k == Value::K::Str) return v->s;
    return {};
}

const std::vector<double>* f64s(const Graph& g, const Node* n, const char* name) {
    const Value* v = g.field(n, name);
    return (v && v->k == Value::K::VecD) ? &v->vd : nullptr;
}


std::string blendName(std::uint16_t id, std::uint16_t version) {
    switch (id) {
        case 0: return "Normal";
        case 1: return "Darken";
        case 2: return version == 0 ? "Multiply" : "Darker Color";
        case 3: return "Color Burn";
        case 4: return "Lighten";
        case 5: return "Screen";
        case 6: return version == 0 ? "Color Dodge" : "Lighter Color";
        case 7: return "Add";
        case 8: return "Overlay";
        case 9: return "Soft Light";
        case 10: return "Hard Light";
        case 11: return "Vivid Light";
        case 12: return "Pin Light";
        case 13: return "Hard Mix";
        case 14: return "Difference";
        case 15: return version == 0 ? "Exclusion" : "Linear Light";
        case 16: return "Subtract";
        case 17: return "Hue";
        case 18: return "Saturation";
        case 19: return "Luminosity";
        case 20: return "Color";
        default: return "Normal";
    }
}

}  // namespace af_detail
}  // namespace pittore::io
