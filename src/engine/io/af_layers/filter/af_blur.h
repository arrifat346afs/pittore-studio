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

enum class LiveBlurKind {
    Gaussian, Box, Motion, Radial, Maximum, Median, DustAndScratches, HighPass, Unsharp
};

struct LiveBlur {
    LiveBlurKind kind = LiveBlurKind::Gaussian;
    double radius = 0.0;
    double angleRad = 0.0;
    double angleDeg = 0.0;
    double cx = 0.0, cy = 0.0;
    double tolerance = 0.0;
    double factor = 0.0;
    double threshold = 0.0;
    bool perChannel = false;
    bool circular = false;
    bool mono = false;
};

struct Vignette {
    double exposure = 0.0, hardness = 0.0, scale = 0.0, shape = 0.0;
};

std::vector<float> boxKernel(double width);
std::vector<float> box3Kernel(double width);
std::vector<float> toPremultiplied(const std::vector<std::uint8_t>& pixels);
std::uint8_t* writePremultiplied(std::uint8_t* px, const float acc[4]);
std::vector<std::uint8_t> fromPremultiplied(const std::vector<float>& buf);
std::vector<float> blurPass(std::size_t w, std::size_t h, const std::vector<float>& src,
                            const std::vector<float>& kernel, bool horizontal);
std::vector<std::uint8_t> separableBlur(std::size_t w, std::size_t h,
                                        const std::vector<std::uint8_t>& pixels,
                                        const std::vector<float>& kernel);
std::array<float, 4> sampleBilinear(std::size_t w, std::size_t h,
                                    const std::vector<std::uint8_t>& pixels, double sx, double sy);
template <class F>
std::vector<std::uint8_t> gatherTaps(std::size_t w, std::size_t h,
                                     const std::vector<std::uint8_t>& pixels, F taps);
std::vector<std::uint8_t> unsharpBlur(std::size_t w, std::size_t h,
                                      const std::vector<std::uint8_t>& pixels, double radius,
                                      double factor, double threshold);
std::vector<std::uint8_t> highPassBlur(std::size_t w, std::size_t h,
                                       const std::vector<std::uint8_t>& pixels, double radius,
                                       bool mono);
std::vector<std::uint8_t> windowMax1D(std::size_t w, std::size_t h,
                                      const std::vector<std::uint8_t>& pixels, std::int64_t r,
                                      bool horizontal);
std::vector<std::uint8_t> windowMaxDisc(std::size_t w, std::size_t h,
                                        const std::vector<std::uint8_t>& pixels, std::int64_t r);
std::vector<std::uint8_t> medianFilter(std::size_t w, std::size_t h,
                                       const std::vector<std::uint8_t>& pixels, std::int64_t r);
std::vector<std::uint8_t> liveBlurApply(std::uint32_t width, std::uint32_t height,
                                        const std::vector<std::uint8_t>& pixels,
                                        const LiveBlur& blur);
std::vector<std::uint8_t> vignetteApply(std::uint32_t width, std::uint32_t height,
                                        const std::vector<std::uint8_t>& pixels,
                                        const Vignette& v);

}  // namespace af_detail
}  // namespace pittore::io
