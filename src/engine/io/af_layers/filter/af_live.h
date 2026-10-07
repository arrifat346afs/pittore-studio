#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "engine/io/af_layers.h"
#include "engine/render/layer_style.h"
#include "engine/text/text_engine.h"
#include "engine/vector/vector_art.h"
#include "engine/vector/vector_shape.h"

#include "engine/io/af_layers/graph/af_graph.h"
#include "engine/io/af_layers/geom/af_homography.h"
#include "engine/io/af_layers/filter/af_distort.h"
#include "engine/io/af_layers/filter/af_blur.h"
namespace pittore::io {
namespace af_detail {

// Live filters ("FlRN" nodes under Pixel > New Live Filter) rework a layer's
// own pixels in place: geometric distortions, the blurs, and the vignette. A
// later filter in the AdCh list works on the earlier one's output; a Live
constexpr float kFxBlurRadi = 0.58f;  // .af "Radi" blur -> our radius
struct LiveFilter {
    enum Kind { KindGeometry, KindBlur, KindVignette } kind = KindBlur;
    Distort distort;
    LiveBlur blur;
    Vignette vignette;
};

std::optional<std::array<std::pair<double, double>, 4>> quadOf(const Graph& g, const Node* filt,
                                                               const char* name);
bool filterIsIdentity(const Graph& g, const Node* node);
std::optional<LiveFilter> distortOf(const Graph& g, const Node* flrn);
bool nodeIsFlrn(const Node* n);
std::vector<LiveFilter> liveFilters(const Graph& g, const Node* node);
std::optional<Homography> warpOf(const Graph& g, const Node* flrn);
std::optional<Homography> liveWarp(const Graph& g, const Node* node);

}  // namespace af_detail
}  // namespace pittore::io
