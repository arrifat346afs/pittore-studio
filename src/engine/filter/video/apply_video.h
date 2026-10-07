#pragma once
// Video filters. Split from engine/filter/filters.h.
#include <string>
#include <vector>

#include "engine/core/image.h"
#include "engine/filter/core/filter_detail.h"
#include "engine/filter/core/filter_params.h"

namespace pittore::filter {

inline bool applyVideo(Image& img, Image& /*scratch*/, const std::string& id,
                       const std::vector<double>& par) {
    using namespace detail;
    const std::uint32_t w = img.width();
    const std::uint32_t h = img.height();
    auto p0 = pv(par, 0, 0.0);
    auto p1 = pv(par, 1, 0.0);
    if (id == "deinterlace") {
        const bool even = std::round(p0) > 0.5;
        const bool dupe = std::round(p1) > 0.5;
        for (std::uint32_t y = 0; y < h; ++y) {
            const bool drop = even ? (y % 2 == 0) : (y % 2 == 1);
            if (!drop || y == 0 || y + 1 >= h) continue;
            for (std::uint32_t x = 0; x < w; ++x) {
                RGBAf &t = img.at(x, y);
                if (dupe) {
                    const float keepA = t.a;
                    t = img.at(x, y - 1);
                    t.a = keepA;
                } else {
                    const RGBAf &a = img.at(x, y - 1);
                    const RGBAf &b = img.at(x, y + 1);
                    t.r = (a.r + b.r) * 0.5f;
                    t.g = (a.g + b.g) * 0.5f;
                    t.b = (a.b + b.b) * 0.5f;
                }
            }
        }
        return true;
    }
    if (id == "ntsc_colors") {
        const double m = strengthOf(par);
        if (m <= 0.0) return true;
        Image orig = img.clone();
        for (std::size_t i = 0; i < img.pixel_count(); ++i) {
            for (int c = 0; c < 3; ++c)
                img.data()[i][c] = static_cast<float>(std::floor(c01(img.data()[i][c]) * 31.0 + 0.5) / 31.0);
        }
        blendInto(img, orig, m);
        return true;
    }
    return false;
}

}  // namespace pittore::filter
