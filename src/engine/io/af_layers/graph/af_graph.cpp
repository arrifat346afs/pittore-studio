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


float f32FromBits(std::uint32_t b) {
    float f;
    std::memcpy(&f, &b, 4);
    return f;
}

double f64FromBits(std::uint64_t b) {
    double d;
    std::memcpy(&d, &b, 8);
    return d;
}


Graph parseGraph(const std::vector<std::uint8_t>& bytes) {
    GraphParser p;
    p.c = Cursor(bytes.data(), bytes.size());
    if (p.c.u32() != kTagDoc) fail("entry is not an object-graph document");
    const std::uint16_t fileVersion = p.c.u16();
    const std::uint32_t rootTag = p.c.u32();
    const std::uint16_t typeVersion = p.c.u16();
    if (fileVersion > 2) fail("object graph version is unknown");
    std::uint32_t headerExtra = 0;
    if (fileVersion == 2) headerExtra = p.c.u32();  // header extra word
    Node* root = p.newNode();
    root->types.emplace_back(rootTag, typeVersion);
    root->fields = p.fields(true);
    p.g.fileVersion = fileVersion;
    p.g.headerExtra = headerExtra;
    return std::move(p.g);
}

}  // namespace af_detail
}  // namespace pittore::io
