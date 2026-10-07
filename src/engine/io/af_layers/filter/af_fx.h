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
#include "engine/io/af_layers/geom/af_geom.h"
namespace pittore::io {
namespace af_detail {

render::StyleBlend fxBlendOf(std::uint16_t id, std::uint16_t version);
std::optional<render::StyleColor> fxColor(const Graph& g, const Node* colr);
bool fxGradientStops(const Graph& g, const Node* fill, render::StyleColor& from, render::StyleColor& to, bool& radial);
render::LayerStyle fxParse(const Graph& g, const Node* node, const Mat& ctm,
                std::vector<std::string>& skipped);
std::optional<float> f32Last(const Node* n, const char* name);
std::optional<std::uint16_t> u16Of(const Graph& g, const Node* n, const char* name);
std::array<float, 3> hslToRgb(float h, float s, float l);
std::optional<std::array<std::uint8_t, 4>> vectorColorBytes(const Graph& g,
                                                            const Node* colr);
std::optional<std::array<std::uint8_t, 4>> fillColorBytes(const Graph& g,
                                                          const Node* fill);
std::vector<pittore::vector::GradientStop> vectorGradientStops(const Graph& g,
                                                                const Node* fill);
std::optional<pittore::vector::GradientFill> vectorGradientFill(const Graph& g,
                                                                 const Node* fill,
                                                                 const Node* host);

}  // namespace af_detail
}  // namespace pittore::io
