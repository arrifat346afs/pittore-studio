#include "engine/ai/bg_remove.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "engine/core/log.h"

#ifdef PITTORE_HAS_ONNX
#include <onnxruntime_cxx_api.h>
#endif

#include "engine/ai/shared/ai_session.h"
#include "engine/ai/shared/ai_preprocess.h"

namespace pittore::ai {
namespace ai_detail {

#ifdef PITTORE_HAS_ONNX
// ImageNet mean/std used by rembg's BaseSession for every family in the
// catalogue (u2net / isnet / birefnet all consume the same normalisation).
constexpr float kMean[3] = {0.485f, 0.456f, 0.406f};
constexpr float kStd[3] = {0.229f, 0.224f, 0.225f};

void resizeRgba8ToNchwRgb(const std::uint8_t* src, int w, int h, float* dst, int S,
                          bool normalize) {
    for (int y = 0; y < S; ++y) {
        const double fy = (h > 1) ? (y + 0.5) * h / S - 0.5 : 0.0;
        const double fyf = std::floor(fy);
        const int y0 = std::clamp(int(fyf), 0, h - 1);
        const int y1 = std::clamp(std::min(int(fyf) + 1, h - 1), 0, h - 1);
        const float wy = static_cast<float>(fy - fyf);
        const float wy0 = 1.0f - wy, wy1 = wy;
        for (int x = 0; x < S; ++x) {
            const double fx = (w > 1) ? (x + 0.5) * w / S - 0.5 : 0.0;
            const double fxf = std::floor(fx);
            const int x0 = std::clamp(int(fxf), 0, w - 1);
            const int x1 = std::clamp(std::min(int(fxf) + 1, w - 1), 0, w - 1);
            const float wx = static_cast<float>(fx - fxf);
            const float wx0 = 1.0f - wx, wx1 = wx;
            for (int c = 0; c < 3; ++c) {
                const auto px = [&](int yy, int xx) {
                    return src[(std::size_t(yy) * w + xx) * 4 + c] * (1.0f / 255.0f);
                };
                const float v = px(y0, x0) * wx0 * wy0 + px(y0, x1) * wx1 * wy0 +
                                px(y1, x0) * wx0 * wy1 + px(y1, x1) * wx1 * wy1;
                dst[std::size_t(c) * S * S + std::size_t(y) * S + x] =
                    normalize ? (v - kMean[c]) / kStd[c] : v * 255.0f;
            }
        }
    }
}

std::vector<float> resizeMaskToSize(const float* src, int sw, int sh, int w, int h) {
    std::vector<float> out(std::size_t(w) * std::size_t(h));
    for (int y = 0; y < h; ++y) {
        const double fy = (sh > 1) ? (y + 0.5) * sh / h - 0.5 : 0.0;
        const double fyf = std::floor(fy);
        const int y0 = std::clamp(int(fyf), 0, sh - 1);
        const int y1 = std::clamp(std::min(int(fyf) + 1, sh - 1), 0, sh - 1);
        const float wy = static_cast<float>(fy - fyf);
        const float wy0 = 1.0f - wy, wy1 = wy;
        for (int x = 0; x < w; ++x) {
            const double fx = (sw > 1) ? (x + 0.5) * sw / w - 0.5 : 0.0;
            const double fxf = std::floor(fx);
            const int x0 = std::clamp(int(fxf), 0, sw - 1);
            const int x1 = std::clamp(std::min(int(fxf) + 1, sw - 1), 0, sw - 1);
            const float wx = static_cast<float>(fx - fxf);
            const float wx0 = 1.0f - wx, wx1 = wx;
            out[std::size_t(y) * w + x] =
                src[std::size_t(y0) * sw + x0] * wx0 * wy0 +
                src[std::size_t(y0) * sw + x1] * wx1 * wy0 +
                src[std::size_t(y1) * sw + x0] * wx0 * wy1 +
                src[std::size_t(y1) * sw + x1] * wx1 * wy1;
        }
    }
    return out;
}

#endif

}  // namespace ai_detail
}  // namespace pittore::ai
