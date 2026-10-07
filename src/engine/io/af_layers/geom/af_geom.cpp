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

Mat translation(double tx, double ty) { return Mat{{1.0, 0.0, tx, 0.0, 1.0, ty}}; }

Mat matThen(const Mat& a, const Mat& b) {
    return Mat{{
        a.m[0] * b.m[0] + a.m[1] * b.m[3],
        a.m[0] * b.m[1] + a.m[1] * b.m[4],
        a.m[0] * b.m[2] + a.m[1] * b.m[5] + a.m[2],
        a.m[3] * b.m[0] + a.m[4] * b.m[3],
        a.m[3] * b.m[1] + a.m[4] * b.m[4],
        a.m[3] * b.m[2] + a.m[4] * b.m[5] + a.m[5],
    }};
}

std::pair<double, double> matApply(const Mat& m, double x, double y) {
    return {m.m[0] * x + m.m[1] * y + m.m[2], m.m[3] * x + m.m[4] * y + m.m[5]};
}

bool matAxisAligned(const Mat& m) {
    const double scale = std::max({std::abs(m.m[0]), std::abs(m.m[1]), std::abs(m.m[3]),
                                   std::abs(m.m[4])});
    return std::abs(m.m[1]) <= scale * 1e-9 && std::abs(m.m[3]) <= scale * 1e-9;
}

bool matInvert(const Mat& m, Mat& out) {
    const double det = m.m[0] * m.m[4] - m.m[1] * m.m[3];
    if (std::abs(det) < 1e-12) return false;
    const double a = m.m[4] / det, b = -m.m[1] / det, c = -m.m[3] / det, d = m.m[0] / det;
    out = Mat{{a, b, -(a * m.m[2] + b * m.m[5]), c, d, -(c * m.m[2] + d * m.m[5])}};
    return true;
}


// Saturating float-to-byte conversion: NaN becomes 0, values clamp to 0..255
// and truncate.
std::uint8_t byteClamp(double v) {
    if (!(v > 0.0)) return 0u;
    if (v >= 255.0) return 255u;
    return static_cast<std::uint8_t>(v);
}

double roundTiesEven(double v) { return std::nearbyint(v); }


std::int64_t floorDiv(std::int64_t a, std::int64_t b) {
    std::int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}


double matScaleX(const Mat& m) { return std::hypot(m.m[0], m.m[3]); }

double matScaleY(const Mat& m) { return std::hypot(m.m[1], m.m[4]); }

}  // namespace af_detail
}  // namespace pittore::io
