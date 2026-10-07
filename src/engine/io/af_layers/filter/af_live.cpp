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


// One of a filter's corner quads: eight F64 fields in file order — the four xs,
// then the four ys — going top-left, bottom-left, bottom-right, top-right.
std::optional<std::array<std::pair<double, double>, 4>> quadOf(const Graph& g, const Node* filt,
                                                               const char* name) {
    Node* q = g.child(filt, name);
    if (!q) return std::nullopt;
    std::vector<double> v;
    for (const auto& f : q->fields)
        if (f.second.k == Value::K::F64) v.push_back(f.second.f);
    if (v.size() < 8) return std::nullopt;
    for (int i = 0; i < 8; ++i)
        if (!std::isfinite(v[i])) return std::nullopt;
    return std::array<std::pair<double, double>, 4>{
        {{v[0], v[4]}, {v[1], v[5]}, {v[2], v[6]}, {v[3], v[7]}}};
}


bool filterIsIdentity(const Graph& g, const Node* node) {
    Node* filt = g.child(node, "Filt");
    if (!filt) return false;
    const char* pairs[3][2] = {{"DSrA", "DDsA"}, {"DSrB", "DDsB"}, {"Src ", "Dst "}};
    bool sawQuad = false;
    for (const auto& pr : pairs) {
        const auto a = quadOf(g, filt, pr[0]);
        const auto b = quadOf(g, filt, pr[1]);
        if (a && b) {
            sawQuad = true;
            for (int i = 0; i < 4; ++i)
                if (std::abs((*a)[i].first - (*b)[i].first) > 1e-6 ||
                    std::abs((*a)[i].second - (*b)[i].second) > 1e-6)
                    return false;
        } else if (!a && !b) {
            continue;
        } else {
            return false;
        }
    }
    return sawQuad;
}


// The geometric or blurring filter one FlRN applies, if we know how to run it.
std::optional<LiveFilter> distortOf(const Graph& g, const Node* flrn) {
    Node* filt = g.child(flrn, "Filt");
    if (!filt) return std::nullopt;
    auto centre = [&](const char* tag) -> std::pair<double, double> {
        const std::vector<double>* v = f64s(g, filt, tag);
        if (v && v->size() >= 2) return {(*v)[0], (*v)[1]};
        return {0.0, 0.0};
    };
    auto num = [&](const char* tag) -> double {
        const Value* v = g.field(filt, tag);
        if (v && v->k == Value::K::F64) return v->f;
        if (v && v->k == Value::K::F32) return static_cast<double>(v->f);
        return 0.0;
    };
    const std::uint32_t t = g.typeTag(filt);
    LiveFilter out;
    if (t == tag4("RTwC")) {
        const auto [cx, cy] = centre("Orin");
        out.kind = LiveFilter::KindGeometry;
        out.distort.kind = DistortKind::Twirl;
        out.distort.cx = cx;
        out.distort.cy = cy;
        out.distort.radius = num("Radi");
        out.distort.angleDeg = num("Angl");
        return out;
    }
    if (t == tag4("RPPC")) {
        const auto [cx, cy] = centre("Orig");
        out.kind = LiveFilter::KindGeometry;
        out.distort.kind = DistortKind::Pinch;
        out.distort.cx = cx;
        out.distort.cy = cy;
        out.distort.radius = num("Radi");
        out.distort.amount = num("Inte") / 100.0;
        return out;
    }
    if (t == tag4("RSpC")) {
        const auto [cx, cy] = centre("Orig");
        out.kind = LiveFilter::KindGeometry;
        out.distort.kind = DistortKind::Spherical;
        out.distort.cx = cx;
        out.distort.cy = cy;
        out.distort.radius = num("Radi");
        out.distort.amount = num("Inte") / 100.0;
        return out;
    }
    if (t == tag4("RRiC")) {
        const auto [cx, cy] = centre("Orig");
        out.kind = LiveFilter::KindGeometry;
        out.distort.kind = DistortKind::Ripple;
        out.distort.cx = cx;
        out.distort.cy = cy;
        out.distort.intensity = num("Inte");
        return out;
    }
    if (t == tag4("RLdC")) {
        const auto [cx, cy] = centre("Orig");
        out.kind = LiveFilter::KindGeometry;
        out.distort.kind = DistortKind::Lens;
        out.distort.cx = cx;
        out.distort.cy = cy;
        out.distort.radX = num("RadX");
        out.distort.radY = num("RadY");
        out.distort.amount = num("Inte");
        return out;
    }
    if (t == tag4("RPxC")) {
        out.kind = LiveFilter::KindGeometry;
        out.distort.kind = DistortKind::Pixelate;
        out.distort.size = num("Quan");
        return out;
    }
    if (t == tag4("RGBC")) {
        out.kind = LiveFilter::KindBlur;
        out.blur.kind = LiveBlurKind::Gaussian;
        out.blur.radius = num("Radi");
        return out;
    }
    if (t == tag4("RBBC")) {
        out.kind = LiveFilter::KindBlur;
        out.blur.kind = LiveBlurKind::Box;
        out.blur.radius = num("Radi");
        return out;
    }
    if (t == tag4("RMoB")) {
        out.kind = LiveFilter::KindBlur;
        out.blur.kind = LiveBlurKind::Motion;
        out.blur.radius = num("Radi");
        out.blur.angleRad = num("Angl");
        return out;
    }
    if (t == tag4("RRaB")) {
        const auto [cx, cy] = centre("Cent");
        out.kind = LiveFilter::KindBlur;
        out.blur.kind = LiveBlurKind::Radial;
        out.blur.cx = cx;
        out.blur.cy = cy;
        out.blur.angleDeg = num("Angl");
        return out;
    }
    if (t == tag4("RMBC")) {
        out.kind = LiveFilter::KindBlur;
        out.blur.kind = LiveBlurKind::Maximum;
        out.blur.radius = num("Radi");
        out.blur.circular = boolOf(g, filt, "Circ").value_or(false);
        return out;
    }
    if (t == tag4("RMeB")) {
        out.kind = LiveFilter::KindBlur;
        out.blur.kind = LiveBlurKind::Median;
        out.blur.radius = num("Radi");
        return out;
    }
    if (t == tag4("RVgC")) {
        out.kind = LiveFilter::KindVignette;
        out.vignette.exposure = num("Expo");
        out.vignette.hardness = num("Hard");
        out.vignette.scale = num("Scal");
        out.vignette.shape = num("Shap");
        return out;
    }
    if (t == tag4("D&SC")) {
        out.kind = LiveFilter::KindBlur;
        out.blur.kind = LiveBlurKind::DustAndScratches;
        out.blur.radius = num("Radi");
        out.blur.tolerance = num("Tole");
        out.blur.perChannel = boolOf(g, filt, "Chan").value_or(false);
        return out;
    }
    if (t == tag4("RHPC")) {
        out.kind = LiveFilter::KindBlur;
        out.blur.kind = LiveBlurKind::HighPass;
        out.blur.radius = num("Radi");
        out.blur.mono = boolOf(g, filt, "Mono").value_or(false);
        return out;
    }
    if (t == tag4("RUSC")) {
        out.kind = LiveFilter::KindBlur;
        out.blur.kind = LiveBlurKind::Unsharp;
        out.blur.radius = num("Radi");
        out.blur.factor = num("Fact");
        out.blur.threshold = num("Thrs");
        return out;
    }
    return std::nullopt;
}


bool nodeIsFlrn(const Node* n) {
    for (const auto& ty : n->types)
        if (ty.first == tag4("FlRN")) return true;
    return false;
}


// Every live filter we can run on a node, in AdCh order.
std::vector<LiveFilter> liveFilters(const Graph& g, const Node* node) {
    std::vector<LiveFilter> out;
    for (Node* f : g.children(node, "AdCh")) {
        if (!nodeIsFlrn(f)) continue;
        const auto vis = boolOf(g, f, "Visi");
        if (vis && !*vis) continue;
        if (auto d = distortOf(g, f)) out.push_back(*d);
    }
    return out;
}


// The projective map one live filter applies, or none if it is not a warp.
std::optional<Homography> warpOf(const Graph& g, const Node* flrn) {
    Node* filt = g.child(flrn, "Filt");
    if (!filt) return std::nullopt;
    if (boolOf(g, filt, "DMod").value_or(false)) return std::nullopt;
    const auto src = quadOf(g, filt, "Src ");
    const auto dst = quadOf(g, filt, "Dst ");
    if (!src || !dst) return std::nullopt;
    Homography h;
    if (!homographyFromQuads(*src, *dst, h)) return std::nullopt;
    if (homographyIsIdentity(h)) return std::nullopt;
    return h;
}


// Every warp on a node, composed into one map; a later filter maps the
// earlier one's output.
std::optional<Homography> liveWarp(const Graph& g, const Node* node) {
    std::optional<Homography> out;
    for (Node* f : g.children(node, "AdCh")) {
        if (!nodeIsFlrn(f)) continue;
        const auto vis = boolOf(g, f, "Visi");
        if (vis && !*vis) continue;
        const auto h = warpOf(g, f);
        if (!h) continue;
        out = out ? homographyCompose(*h, *out) : *h;
    }
    return out;
}

// Masks use the public AfMask document-space reveal buffers (see
// af_layers.h). Outside their own bounds a mask reveals.

}  // namespace af_detail
}  // namespace pittore::io
