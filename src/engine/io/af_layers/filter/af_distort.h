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

namespace pittore::io {
namespace af_detail {

// The ripple's amplitude convention: a behavioural constant chosen so the
// filter matches the amplitude users expect from ripple effects generally.
// Numeric constants are original fits, not copied from any implementation.
constexpr double kPi = 3.14159265358979323846;
constexpr double kTau = 6.28318530717958647692;

enum class DistortKind { Twirl, Pinch, Spherical, Ripple, Lens, Pixelate };
struct Distort {
    DistortKind kind = DistortKind::Twirl;
    double cx = 0.0, cy = 0.0;
    double radius = 0.0;
    double angleDeg = 0.0;
    double amount = 0.0;
    double intensity = 0.0;
    double radX = 0.0, radY = 0.0;
    double size = 0.0;
};

constexpr double kRippleAmplitude = 2.6224;
constexpr double kRippleExponent = 0.895;

std::pair<double, double> distortSource(const Distort& d, double x, double y);
std::vector<std::uint8_t> pixelateFilter(std::uint32_t width, std::uint32_t height,
                                         const std::vector<std::uint8_t>& pixels, double size);
std::vector<std::uint8_t> distortApply(std::uint32_t width, std::uint32_t height,
                                       const std::vector<std::uint8_t>& pixels,
                                       const Distort& filter);

}  // namespace af_detail
}  // namespace pittore::io
