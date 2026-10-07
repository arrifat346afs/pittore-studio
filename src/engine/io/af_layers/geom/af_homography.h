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

#include "engine/io/af_layers/geom/af_geom.h"
#include "engine/io/af_layers/image/af_image.h"
namespace pittore::io {
namespace af_detail {

// Perspective instead carries a projective warp that is composed into the
// placement map. Every size convention below is the one measured against
// The reference renders of the probe card.
struct Homography {
    double h[9];
};

struct Warped {
    int ox = 0, oy = 0;
    Bitmap img;
};

bool homographyFromQuads(const std::array<std::pair<double, double>, 4>& src,
                         const std::array<std::pair<double, double>, 4>& dst, Homography& out);
std::optional<std::pair<double, double>> homographyApply(const Homography& H, double x, double y);
bool homographyInvert(const Homography& H, Homography& out);
Homography homographyCompose(const Homography& self, const Homography& other);
bool homographyIsIdentity(const Homography& H);
std::optional<Warped> perspectiveResample(const Bitmap& img, const Homography& h);

}  // namespace af_detail
}  // namespace pittore::io
