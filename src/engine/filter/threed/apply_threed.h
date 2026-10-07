#pragma once
// 3D filters. Split from engine/filter/filters.h.
#include <string>
#include <vector>

#include "engine/core/image.h"
#include "engine/filter/core/filter_detail.h"
#include "engine/filter/core/filter_params.h"

namespace pittore::filter {

inline bool applyThreeD(Image& img, Image& scratch, const std::string& id,
                       const std::vector<double>& par) {
    using namespace detail;
    const std::uint32_t w = img.width();
    const std::uint32_t h = img.height();
    auto p0 = pv(par, 0, 0.0);
    auto p1 = pv(par, 1, 0.0);
    if (id == "bump_map") {
        const int blurChoice = std::clamp(static_cast<int>(std::round(p0)), 0, 2);
        const double contrast = 0.5 + std::clamp(p1, 0.0, 100.0) / 100.0;
        const bool invert = std::round(p2v(par)) > 0.5;
        const double s = 1.0;
        Image pre = img.clone();
        if (blurChoice == 0) gaussBlur(pre, 0.5);
        else if (blurChoice == 1) gaussBlur(pre, 1.5);
        else gaussBlur(pre, 3.0);
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                const double gx = lumaF(pre.at(cw(pre, static_cast<int>(x) + 1), y)) - lumaF(pre.at(cw(pre, static_cast<int>(x) - 1), y));
                const double gy = lumaF(pre.at(x, ch(pre, static_cast<int>(y) + 1))) - lumaF(pre.at(x, ch(pre, static_cast<int>(y) - 1)));
                double v = c01(0.5 + (gx + gy) * s * contrast);
                if (invert) v = 1.0 - v;
                RGBAf &t = scratch.at(x, y);
                t.r = t.g = t.b = static_cast<float>(v);
                t.a = img.at(x, y).a;
            }
        }
        std::memcpy(img.data(), scratch.data(), sizeof(RGBAf) * img.pixel_count());
        return true;
    }
    if (id == "normal_map") {
        const int blurChoice = std::clamp(static_cast<int>(std::round(p0)), 0, 2);
        const double contrast = 0.5 + std::clamp(p1, 0.0, 100.0) / 100.0;
        const double s = std::max(0.5, p2v(par)) / 30.0 * 4.0;
        const bool invert = std::round(p3v(par)) > 0.5;
        Image pre = img.clone();
        if (blurChoice == 0) gaussBlur(pre, 0.5);
        else if (blurChoice == 1) gaussBlur(pre, 1.5);
        else gaussBlur(pre, 3.0);
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                const double gx = (lumaF(pre.at(cw(pre, static_cast<int>(x) + 1), y)) - lumaF(pre.at(cw(pre, static_cast<int>(x) - 1), y))) * s * contrast;
                const double gy = (lumaF(pre.at(x, ch(pre, static_cast<int>(y) + 1))) - lumaF(pre.at(x, ch(pre, static_cast<int>(y) - 1)))) * s * contrast;
                double nx = -gx;
                double ny = -gy;
                double nz = 1.0;
                if (invert) {
                    nx = -nx;
                    ny = -ny;
                }
                const double il = 1.0 / std::sqrt(nx * nx + ny * ny + nz * nz);
                RGBAf &t = scratch.at(x, y);
                t.r = static_cast<float>((nx * il + 1) * 0.5);
                t.g = static_cast<float>((ny * il + 1) * 0.5);
                t.b = static_cast<float>((nz * il + 1) * 0.5);
                t.a = img.at(x, y).a;
            }
        }
        std::memcpy(img.data(), scratch.data(), sizeof(RGBAf) * img.pixel_count());
        return true;
    }
    return false;
}

}  // namespace pittore::filter
