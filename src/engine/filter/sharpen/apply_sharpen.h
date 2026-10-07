#pragma once
// Sharpen filters. Split from engine/filter/filters.h.
#include <string>
#include <vector>

#include "engine/core/image.h"
#include "engine/filter/core/filter_detail.h"
#include "engine/filter/core/filter_params.h"

namespace pittore::filter {

inline bool applySharpen(Image& img, Image& /*scratch*/, const std::string& id,
                       const std::vector<double>& par) {
    using namespace detail;
    const std::uint32_t w = img.width();
    const std::uint32_t h = img.height();
    auto p0 = pv(par, 0, 0.0);
    auto p1 = pv(par, 1, 0.0);
    if (id == "sharpen" || id == "sharpen_more") {
        const double m = strengthOf(par);
        if (m <= 0.0) return true;
        Image orig = img.clone();
        unsharpF(img, id == std::string("sharpen") ? 0.6 : 1.2, 1, 0.0);
        blendInto(img, orig, m);
        return true;
    }
    if (id == "sharpen_edges") {
        const double m = strengthOf(par);
        if (m <= 0.0) return true;
        Image orig = img.clone();
        Image blur(w, h);
        boxBlurInto(img, blur, 1);
        for (std::size_t i = 0; i < img.pixel_count(); ++i) {
            const double e = std::fabs(img.data()[i].r - blur.data()[i].r) +
                std::fabs(img.data()[i].g - blur.data()[i].g) +
                std::fabs(img.data()[i].b - blur.data()[i].b);
            if (e > 0.06) {
                for (int c = 0; c < 3; ++c)
                    img.data()[i][c] = static_cast<float>(c01(img.data()[i][c] + 0.5 * (img.data()[i][c] - blur.data()[i][c])));
            }
        }
        blendInto(img, orig, m);
        return true;
    }
    if (id == "smart_sharpen" || id == "shake_reduction") {
        const double amt = p0 / 100.0 * 2.0;
        const int r = id == std::string("shake_reduction") ? 2 : std::max(1, static_cast<int>(std::round(pv(par, 1, 1.5))));
        const double nz = id == std::string("shake_reduction") ? 0.0 : std::clamp(p2v(par), 0.0, 100.0) / 100.0;
        const int rm = id == std::string("shake_reduction") ? 0 : std::clamp(static_cast<int>(std::round(p3v(par))), 0, 2);
        const double ang = (id == std::string("shake_reduction") ? 0.0 : p4v(par)) * 3.141592653589793 / 180.0;
        Image base = img.clone();
        if (nz > 0.01) {
            medianR(base, 1);
            for (std::size_t i = 0; i < img.pixel_count(); ++i) {
                for (int c = 0; c < 3; ++c)
                    base.data()[i][c] = static_cast<float>(static_cast<double>(img.data()[i][c]) * (1 - nz * 0.5) + static_cast<double>(base.data()[i][c]) * nz * 0.5);
            }
        }
        Image blur(w, h);
        if (rm == 2) {
            const double dx = std::cos(ang);
            const double dy = std::sin(ang);
            for (std::uint32_t y = 0; y < h; ++y) {
                for (std::uint32_t x = 0; x < w; ++x) {
                    double ar = 0, ag = 0, ab = 0;
                    for (int i = -r; i <= r; ++i) {
                        RGBAf s = bilin(base, static_cast<double>(x) + dx * i, static_cast<double>(y) + dy * i);
                        ar += s.r;
                        ag += s.g;
                        ab += s.b;
                    }
                    const double n = 2 * r + 1;
                    RGBAf &d = blur.at(x, y);
                    d.r = static_cast<float>(ar / n);
                    d.g = static_cast<float>(ag / n);
                    d.b = static_cast<float>(ab / n);
                    d.a = base.at(x, y).a;
                }
            }
        } else if (rm == 1) {
            for (std::uint32_t y = 0; y < h; ++y) {
                for (std::uint32_t x = 0; x < w; ++x) {
                    double ar = 0, ag = 0, ab = 0;
                    int n = 0;
                    for (int dy = -r; dy <= r; ++dy) {
                        for (int dx = -r; dx <= r; ++dx) {
                            if (dx * dx + dy * dy > r * r) continue;
                            const RGBAf &s = base.at(cw(base, static_cast<int>(x) + dx), ch(base, static_cast<int>(y) + dy));
                            ar += s.r;
                            ag += s.g;
                            ab += s.b;
                            ++n;
                        }
                    }
                    n = std::max(1, n);
                    RGBAf &d = blur.at(x, y);
                    d.r = static_cast<float>(ar / n);
                    d.g = static_cast<float>(ag / n);
                    d.b = static_cast<float>(ab / n);
                    d.a = base.at(x, y).a;
                }
            }
        } else {
            boxBlurInto(base, blur, r);
        }
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                const RGBAf &o = img.at(x, y);
                const RGBAf &b = blur.at(x, y);
                for (int c = 0; c < 3; ++c) {
                    const double diff = static_cast<double>(o[c]) - static_cast<double>(b[c]);
                    if (std::fabs(diff) <= 0.01) continue;
                    img.at(x, y)[c] = static_cast<float>(c01(static_cast<double>(o[c]) + amt * diff));
                }
            }
        }
        return true;
    }
    if (id == "unsharp_mask") {
        unsharpF(img, p0 / 100.0, std::max(1, static_cast<int>(std::round(p1))), p2v(par) / 255.0);
        return true;
    }
    return false;
}

}  // namespace pittore::filter
