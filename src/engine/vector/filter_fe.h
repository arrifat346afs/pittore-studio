#pragma once
// SVG filter primitives (fe*): model + CPU application.
//
// The editor keeps filters as data (FilterGraph) so the Filter Editor can
// tweak them, SVG import/export round-trips them, and the rasterizer applies
// them on the CPU tile path (GPU parity comes from the same math). Only the
// widely-used primitives are rasterized; the rest round-trip as data.
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace pittore::vector {

// One primitive in a filter chain.
struct FePrimitive {
    std::string type;  // "feGaussianBlur", "feTurbulence", ...
    std::map<std::string, std::string> attrs;
    std::string result;  // result="" name for sub-sequent `in=` refs
    std::string in;      // in= / in2= collapsed: first input name
    std::string in2;
};

struct FilterGraph {
    std::string id;
    double x = -0.1, y = -0.1, width = 1.2, height = 1.2;  // filter region
    std::string units;  // filterUnits/primitiveUnits raw (round-trip)
    std::vector<FePrimitive> prims;
};

// RGBA8 image, row-major straight alpha.
struct RgbaImage {
    int w = 0, h = 0;
    std::vector<std::uint8_t> px;
};

// Apply the graph to `src` (same-size output; empty graph = copy).
RgbaImage applyFilterGraph(const FilterGraph& graph, const RgbaImage& src);

// Parse <filter> children into a graph (unknown primitives kept as data).
FilterGraph filterGraphFromSvg(const std::string& id,
                               const std::vector<FePrimitive>& prims);
// Serialize back to <filter>...</filter> text.
std::string filterGraphToSvg(const FilterGraph& graph);

// Filter Gallery: 250-entry catalogue metadata (name, category, graph stub).
// Categories are Blur, Color, Distort, Light, Morph, Noise, ... Raster-only
// Pittore filters map onto these where possible.
struct FilterCatalogEntry {
    std::string id;
    std::string label;
    std::string category;
};
const std::vector<FilterCatalogEntry>& svgFilterCatalog();

}  // namespace pittore::vector
