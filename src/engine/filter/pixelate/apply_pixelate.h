#pragma once
// Pixelate filters. Split from engine/filter/filters.h.
#include <string>
#include <vector>

#include "engine/core/image.h"
#include "engine/filter/core/filter_detail.h"
#include "engine/filter/core/filter_params.h"

namespace pittore::filter {

// Umbrella dispatcher (engine/filter/filters.h); defined after this header.
inline void applyFilter(Image& img, const std::string& id,
                        const std::vector<double>& par);

inline bool applyPixelate(Image& img, Image& scratch, const std::string& id,
                       const std::vector<double>& par) {
    using namespace detail;
    const std::uint32_t w = img.width();
    const std::uint32_t h = img.height();
    auto p0 = pv(par, 0, 0.0);
    auto p1 = pv(par, 1, 0.0);
    if (id == "mosaic" || id == "crystallize" || id == "pointillize") {
        int cell = std::max(2, static_cast<int>(std::round(p0)));
        for (std::uint32_t y0 = 0; y0 < h; y0 += cell) {
            for (std::uint32_t x0 = 0; x0 < w; x0 += cell) {
                const std::uint32_t sx = std::min(x0 + cell / 2, w - 1);
                const std::uint32_t sy = std::min(y0 + cell / 2, h - 1);
                const RGBAf v = img.at(sx, sy);
                for (std::uint32_t y = y0; y < std::min(y0 + cell, h); ++y) {
                    for (std::uint32_t x = x0; x < std::min(x0 + cell, w); ++x)
                        scratch.at(x, y) = v;
                }
            }
        }
        std::memcpy(img.data(), scratch.data(), sizeof(RGBAf) * img.pixel_count());
        return true;
    }
    if (id == "facet") {
        const double m = strengthOf(par);
        if (m <= 0.0) return true;
        Image orig = img.clone();
        applyFilter(img, "mosaic", {6.0});
        blendInto(img, orig, m);
        return true;
    }
    if (id == "fragment") {
        const int off = std::max(1, static_cast<int>(std::round(p0)));
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                const RGBAf a = img.at(cw(img, static_cast<int>(x) - off), y);
                const RGBAf b = img.at(cw(img, static_cast<int>(x) + off), y);
                const RGBAf c = img.at(x, ch(img, static_cast<int>(y) - off));
                const RGBAf d = img.at(x, ch(img, static_cast<int>(y) + off));
                RGBAf &t = scratch.at(x, y);
                t.r = (a.r + b.r + c.r + d.r) * 0.25f;
                t.g = (a.g + b.g + c.g + d.g) * 0.25f;
                t.b = (a.b + b.b + c.b + d.b) * 0.25f;
                t.a = img.at(x, y).a;
            }
        }
        std::memcpy(img.data(), scratch.data(), sizeof(RGBAf) * img.pixel_count());
        return true;
    }
    if (id == "mezzotint") {
        const int type = std::clamp(static_cast<int>(std::round(p0)), 0, 9);
        const double grain = std::max(1.0, p1);
        const bool lineal = type >= 4;
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                const double l = lumaF(img.at(x, y));
                double v = 0;
                if (!lineal) {
                    const double cell = grain * (type == 3 ? 2.2 : 1.0);
                    const double px = std::fmod(static_cast<double>(x) / cell, 1.0);
                    const double py = std::fmod(static_cast<double>(y) / cell, 1.0);
                    const double th = type == 2 ? 0.35 + 0.3 * vnoise(x * 0.2, y * 0.2, 11) : 0.5;
                    const double dot = (px - 0.5) * (px - 0.5) + (py - 0.5) * (py - 0.5);
                    v = dot < (1.0 - l) * 0.25 * th * 2.0 ? 0.0 : 1.0;
                } else {
                    const bool vertical = type >= 7;
                    const double coord = (vertical ? x : y) / grain;
                    const double fr = coord - std::floor(coord);
                    const double th = type % 3 == 0 ? 0.45 : 0.5;
                    const bool stroke = type >= 7;
                    if (stroke) {
                        const double n = vnoise(x * 0.1, y * 0.1, 13);
                        v = (fr < l * (0.6 + n * 0.8) && fr > th * 0.3) ? 0.0 : 1.0;
                    } else {
                        v = fr < l ? 0.0 : 1.0;
                        if (fr > th + 0.4) v = 1.0 - v;
                    }
                }
                RGBAf &t = img.at(x, y);
                t.r = t.g = t.b = static_cast<float>(v);
            }
        }
        return true;
    }
    if (id == "color_halftone") {
        const int cell = std::max(4, static_cast<int>(std::round(p0)));
        const double ang[4] = {p1 * 3.141592653589793 / 180.0, p2v(par) * 3.141592653589793 / 180.0,
                               p3v(par) * 3.141592653589793 / 180.0, p4v(par) * 3.141592653589793 / 180.0};
        for (std::uint32_t y0 = 0; y0 < h; y0 += cell) {
            for (std::uint32_t x0 = 0; x0 < w; x0 += cell) {
                const RGBAf v = img.at(std::min(x0 + cell / 2, w - 1), std::min(y0 + cell / 2, h - 1));
                const double chv[3] = {v.r, v.g, v.b};
                for (std::uint32_t y = y0; y < std::min(y0 + cell, h); ++y) {
                    for (std::uint32_t x = x0; x < std::min(x0 + cell, w); ++x) {
                        RGBAf &t = scratch.at(x, y);
                        for (int c = 0; c < 3; ++c) {
                            const double lx = (static_cast<double>(x) - x0 - cell * 0.5) / cell;
                            const double ly = (static_cast<double>(y) - y0 - cell * 0.5) / cell;
                            const double rx = lx * std::cos(ang[c]) - ly * std::sin(ang[c]);
                            const double ry = lx * std::sin(ang[c]) + ly * std::cos(ang[c]);
                            const double dist = std::sqrt(rx * rx + ry * ry);
                            const double dot = dist < (1.0 - chv[c]) * 0.7 ? 0.0 : 1.0;
                            t[c] = static_cast<float>(chv[c] * 0.4 + dot * 0.6);
                        }
                        t.a = v.a;
                    }
                }
            }
        }
        std::memcpy(img.data(), scratch.data(), sizeof(RGBAf) * img.pixel_count());
        return true;
    }
    return false;
}

}  // namespace pittore::filter
