#pragma once
// Sketch filters. Split from engine/filter/filters.h.
#include <string>
#include <vector>

#include "engine/core/image.h"
#include "engine/filter/core/filter_detail.h"
#include "engine/filter/core/filter_params.h"

namespace pittore::filter {

inline bool applySketch(Image& img, Image& /*scratch*/, const std::string& id,
                       const std::vector<double>& par) {
    using namespace detail;
    const std::uint32_t w = img.width();
    const std::uint32_t h = img.height();
    auto p0 = pv(par, 0, 0.0);
    auto p1 = pv(par, 1, 0.0);
    if (id == "halftone_pattern") {
        const double sz = std::max(1.0, p0);
        const double contrast = 1.0 + std::clamp(p1, 0.0, 50.0) / 50.0;
        const int pattern = std::clamp(static_cast<int>(std::round(p2v(par))), 0, 2);
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                const double l = c01((lumaF(img.at(x, y)) - 0.5) * contrast + 0.5);
                double pat = 0;
                if (pattern == 2) {
                    pat = ((x / sz) - std::floor(x / sz) < 0.5) ? 0.0 : 1.0;
                } else if (pattern == 1) {
                    const double dx = std::fmod(x / sz, 1.0) - 0.5;
                    const double dy = std::fmod(y / sz, 1.0) - 0.5;
                    pat = (dx * dx + dy * dy < 0.0625) ? 0.0 : 1.0;
                } else {
                    const double dx = std::fmod(x / sz, 1.0) - 0.5;
                    const double dy = std::fmod(y / sz, 1.0) - 0.5;
                    pat = std::sqrt(dx * dx + dy * dy) < (1.0 - l) * 0.5 ? 0.0 : 1.0;
                }
                const double v = l > 0.5 ? std::max(l, pat * 0.9) : std::min(l, (1 - pat) * 0.1 + l * 0.5);
                RGBAf &t = img.at(x, y);
                t.r = t.g = t.b = static_cast<float>(v);
            }
        }
        return true;
    }
    if (id == "water_paper") {
        const double f = std::max(3.0, p0);
        const double bright = std::clamp(p1, 0.0, 100.0) / 100.0;
        const double contrast = std::clamp(p2v(par), 0.0, 100.0) / 100.0;
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                const double n = fbm(static_cast<double>(x) / f, static_cast<double>(y) / f, 3, 55);
                RGBAf &t = img.at(x, y);
                const double m = (0.85 + n * 0.3) * (0.7 + bright * 0.6);
                t.r = static_cast<float>(c01((t.r * m + 0.04 - 0.5) * (0.6 + contrast * 0.8) + 0.5));
                t.g = static_cast<float>(c01((t.g * m + 0.04 - 0.5) * (0.6 + contrast * 0.8) + 0.5));
                t.b = static_cast<float>(c01((t.b * m + 0.03 - 0.5) * (0.6 + contrast * 0.8) + 0.5));
            }
        }
        return true;
    }
    return false;
}

}  // namespace pittore::filter
