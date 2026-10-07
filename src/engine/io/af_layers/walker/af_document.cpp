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

std::optional<AfLayersDoc> buildDocument(const Archive& ar, const Graph& g,
                                         const std::string& baseDir) {
    AfLayersDoc doc;
    doc.log.push_back("container v" + std::to_string(ar.version) + ": " +
                      std::to_string(ar.entries.size()) + " entries, " +
                      std::to_string(ar.names.size()) + " names");
    Node* root = g.root();
    Node* docNode = g.child(root, "DocR");
    if (!docNode) {
        doc.log.push_back("no document root (DocR)");
        return doc;
    }
    std::vector<Node*> spreads = g.children(docNode, "Chld");
    if (spreads.empty()) {
        doc.log.push_back("document has no spreads");
        return doc;
    }
    Node* spread = spreads[0];
    if (spreads.size() > 1)
        doc.log.push_back("importing first of " + std::to_string(spreads.size()) + " spreads");

    double orgX = 0, orgY = 0;
    std::uint32_t width = 0, height = 0;
    bool fromSprB = false;
    if (const std::vector<double>* sb = f64s(g, spread, "SprB"); sb && sb->size() >= 4) {
        orgX = (*sb)[0];
        orgY = (*sb)[1];
        width = static_cast<std::uint32_t>(std::lround(std::max(0.0, (*sb)[2] - (*sb)[0])));
        height = static_cast<std::uint32_t>(std::lround(std::max(0.0, (*sb)[3] - (*sb)[1])));
        fromSprB = true;
    } else if (const std::vector<double>* ds = f64s(g, docNode, "DfSz"); ds && ds->size() >= 2) {
        width = static_cast<std::uint32_t>(std::lround(std::max(0.0, (*ds)[0])));
        height = static_cast<std::uint32_t>(std::lround(std::max(0.0, (*ds)[1])));
    } else {
        doc.log.push_back("no spread bounds (SprB) or document size (DfSz)");
        return doc;
    }
    if (width == 0 || height == 0 || width > (1 << 20) || height > (1 << 20)) {
        doc.log.push_back("implausible canvas " + std::to_string(width) + "x" +
                          std::to_string(height));
        return doc;
    }
    if (static_cast<std::uint64_t>(width) * height > kMaxPixels) {
        doc.log.push_back("canvas over the pixel cap");
        return doc;
    }
    doc.width = width;
    doc.height = height;
    doc.depth = 8;
    doc.log.push_back("canvas " + std::to_string(width) + "x" + std::to_string(height) +
                      (fromSprB ? " (SprB)" : " (DfSz)"));

    Walker w{ar, g, doc, baseDir, {}, {}};
    const Mat ctm = translation(-orgX, -orgY);

    if (Node* ras = g.child(spread, "RasS")) {
        if (Node* b = g.child(ras, "Bitm"); b && bitmapHasContent(g, b)) {
            std::string why;
            if (auto img = w.decodeBitmap(b, why)) {
                if (auto placed = placeRaster(w.nodeCtm(ras, ctm), std::move(*img))) {
                    AfLayer layer;
                    layer.left = placed->rect.x0;
                    layer.top = placed->rect.y0;
                    layer.width = static_cast<std::uint32_t>(placed->rect.width());
                    layer.height = static_cast<std::uint32_t>(placed->rect.height());
                    layer.name = "Background";
                    layer.rgba.resize(placed->img.px.size());
                    for (std::size_t i = 0; i < placed->img.px.size(); ++i)
                        layer.rgba[i] = static_cast<std::uint16_t>(placed->img.px[i]) * 257u;
                    doc.log.push_back("background raster spread " + std::to_string(layer.width) +
                                      "x" + std::to_string(layer.height));
                    doc.layers.push_back(std::move(layer));
                    ++doc.frameCount;
                }
            }
        }
    }

    for (Node* ch : g.children(spread, "Chld")) w.emit(ch, ctm, 0, false);

    doc.complete = (doc.skippedLayers == 0);
    return doc;
}

}  // namespace af_detail
}  // namespace pittore::io

namespace pittore::io {

std::optional<AfLayersDoc> afDecodeLayers(const std::vector<std::uint8_t>& data,
                                          std::string* error, const std::string& baseDir) {
    using namespace af_detail;
    if (!data.empty() && !(data[0] == 0x00 && data.size() >= 4 && data[1] == 0xff &&
                           data[2] == 0x4b && data[3] == 0x41)) {
        if (error) *error = "not an Affinity archive (bad magic)";
        return std::nullopt;
    }
    try {
        const Archive ar = Archive::parse(data);
        const ArchiveEntry* docEntry = ar.head("doc.dat");
        if (!docEntry) {
            if (error) *error = "container has no doc.dat";
            return std::nullopt;
        }
        const std::vector<std::uint8_t> docBytes = ar.extract(*docEntry);
        const Graph g = parseGraph(docBytes);
        return buildDocument(ar, g, baseDir);
    } catch (const AfError& e) {
        if (error) *error = e.msg;
        return std::nullopt;
    } catch (const std::exception& e) {
        if (error) *error = e.what();
        return std::nullopt;
    }
}

}  // namespace pittore::io
