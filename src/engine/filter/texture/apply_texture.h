#pragma once
// Texture filters. Split from engine/filter/filters.h.
#include <string>
#include <vector>

#include "engine/core/image.h"
#include "engine/filter/core/filter_detail.h"
#include "engine/filter/core/filter_params.h"

namespace pittore::filter {

inline bool applyTexture(Image& img, Image& scratch, const std::string& id,
                       const std::vector<double>& par) {
    using namespace detail;
    const std::uint32_t w = img.width();
    const std::uint32_t h = img.height();
    auto p0 = pv(par, 0, 0.0);
    auto p1 = pv(par, 1, 0.0);
    if (id == "craquelure") {
        const double sp = std::max(2.0, p0);
        const double dp = p1 / 10.0;
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                const double nx = fbm(x / sp, y / sp, 3, 101);
                const double ny = fbm(x / sp + 40, y / sp + 40, 3, 202);
                const double crack = (std::fabs(nx - 0.5) < 0.02 || std::fabs(ny - 0.5) < 0.02) ? 1.0 : 0.0;
                const double lite = std::clamp(p2v(par), 0.0, 10.0) / 10.0;
                RGBAf &t = img.at(x, y);
                t.r = static_cast<float>(c01(t.r - crack * dp * 0.4 + crack * lite * 0.15));
                t.g = static_cast<float>(c01(t.g - crack * dp * 0.4 + crack * lite * 0.15));
                t.b = static_cast<float>(c01(t.b - crack * dp * 0.4 + crack * lite * 0.12));
            }
        }
        return true;
    }
    if (id == "mosaic_tiles" || id == "patchwork" || id == "stained_glass") {
        const int cell = id == std::string("patchwork") ? std::max(8, static_cast<int>(std::round(p0)) * 8 + 8)
            : std::max(2, static_cast<int>(std::round(p0)));
        const int grout = id == std::string("mosaic_tiles") ? std::max(0, static_cast<int>(std::round(p1))) : 1;
        const double groutLight = id == std::string("mosaic_tiles") ? 0.25 + std::clamp(p2v(par), 0.0, 10.0) * 0.05 : 0.25;
        const double relief = id == std::string("patchwork") ? std::clamp(p1, 0.0, 25.0) / 25.0 : 0.0;
        const double cellLight = id == std::string("stained_glass") ? std::clamp(p2v(par), 0.0, 10.0) / 10.0 : 0.0;
        std::uint64_t st = 31337;
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                const std::uint32_t bx = (x / cell) * cell;
                const std::uint32_t by = (y / cell) * cell;
                const bool edge = (x - bx < static_cast<std::uint32_t>(grout)) || (y - by < static_cast<std::uint32_t>(grout));
                if (edge) {
                    RGBAf &t = scratch.at(x, y);
                    const float g = static_cast<float>(groutLight);
                    t.r = t.g = t.b = g;
                    t.a = img.at(x, y).a;
                } else {
                    RGBAf v = img.at(std::min(bx + cell / 2, w - 1), std::min(by + cell / 2, h - 1));
                    if (relief > 0.01) {
                        const double u = (hash2i(static_cast<int>(bx), static_cast<int>(by), st) % 1000) / 1000.0 - 0.5;
                        v.r = static_cast<float>(c01(v.r + u * relief * 0.4));
                        v.g = static_cast<float>(c01(v.g + u * relief * 0.4));
                        v.b = static_cast<float>(c01(v.b + u * relief * 0.4));
                    }
                    if (cellLight > 0.01) {
                        v.r = static_cast<float>(c01(v.r * (1 + cellLight * 0.3)));
                        v.g = static_cast<float>(c01(v.g * (1 + cellLight * 0.3)));
                        v.b = static_cast<float>(c01(v.b * (1 + cellLight * 0.3)));
                    }
                    scratch.at(x, y) = v;
                }
            }
        }
        std::memcpy(img.data(), scratch.data(), sizeof(RGBAf) * img.pixel_count());
        return true;
    }
    if (id == "texturizer") {
        const int tex = std::clamp(static_cast<int>(std::round(p0)), 0, 3);
        const double scaling = std::clamp(p1, 50.0, 200.0) / 100.0;
        const double relief = std::clamp(p2v(par), 0.0, 50.0) / 50.0;
        const int light = std::clamp(static_cast<int>(std::round(p3v(par))), 0, 7);
        const bool invert = std::round(p4v(par)) > 0.5;
        const double la[8] = {1.5707963267948966, 0.7853981633974483, 0.0, -0.7853981633974483,
                              -1.5707963267948966, -2.356194490192345, 3.141592653589793, 2.356194490192345};
        const double ldx = std::cos(la[light]);
        const double ldy = std::sin(la[light]);
        const double sc = (tex == 0 ? 6.0 : tex == 1 ? 10.0 : tex == 2 ? 4.0 : 14.0) * scaling;
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                const double n0 = fbm(static_cast<double>(x) / sc, static_cast<double>(y) / sc, 3, 200 + tex);
                const double nx = fbm((static_cast<double>(x) + ldx * 2.0) / sc, (static_cast<double>(y) + ldy * 2.0) / sc, 3, 200 + tex);
                double bump = (nx - n0) * 8.0 * relief;
                if (invert) bump = -bump;
                RGBAf &t = img.at(x, y);
                t.r = static_cast<float>(c01(t.r + bump * 0.5));
                t.g = static_cast<float>(c01(t.g + bump * 0.5));
                t.b = static_cast<float>(c01(t.b + bump * 0.5));
            }
        }
        return true;
    }
    return false;
}

}  // namespace pittore::filter
