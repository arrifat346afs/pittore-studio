#pragma once
// Noise filters. Split from engine/filter/filters.h.
#include <string>
#include <vector>

#include "engine/core/image.h"
#include "engine/filter/core/filter_detail.h"
#include "engine/filter/core/filter_params.h"

namespace pittore::filter {

inline bool applyNoise(Image& img, Image& /*scratch*/, const std::string& id,
                       const std::vector<double>& par) {
    using namespace detail;
    const std::uint32_t w = img.width();
    const std::uint32_t h = img.height();
    auto p0 = pv(par, 0, 0.0);
    auto p1 = pv(par, 1, 0.0);
    if (id == "despeckle") {
        const double m = strengthOf(par);
        if (m <= 0.0) return true;
        Image orig = img.clone();
        medianR(img, 1);
        blendInto(img, orig, m);
        return true;
    }
    if (id == "median") {
        medianR(img, std::max(1, static_cast<int>(std::round(p0))));
        return true;
    }
    if (id == "dust_scratches") {
        const int r = std::max(1, static_cast<int>(std::round(p0)));
        const double thr = std::clamp(p1, 0.0, 255.0) / 255.0;
        Image med(w, h);
        std::memcpy(med.data(), img.data(), sizeof(RGBAf) * img.pixel_count());
        medianR(med, r);
        for (std::size_t i = 0; i < img.pixel_count(); ++i) {
            const double d = std::fabs(img.data()[i].r - med.data()[i].r) +
                std::fabs(img.data()[i].g - med.data()[i].g) +
                std::fabs(img.data()[i].b - med.data()[i].b);
            if (d > thr * 3.0) {
                for (int c = 0; c < 3; ++c) img.data()[i][c] = med.data()[i][c];
            }
        }
        return true;
    }
    if (id == "reduce_noise" || id == "film_grain") {
        if (id == std::string("reduce_noise")) {
            const double strength = std::clamp(p0, 0.0, 10.0) / 10.0;
            const double detail = std::clamp(p1, 0.0, 100.0) / 100.0;
            const double colour = std::clamp(p2v(par), 0.0, 100.0) / 100.0;
            const double sharp = std::clamp(p3v(par), 0.0, 100.0) / 100.0;
            const bool jpeg = std::round(p4v(par)) > 0.5;
            if (strength > 0.01 || colour > 0.01 || jpeg) {
                medianR(img, 1);
                Image soft = img.clone();
                gaussBlur(soft, 0.6 + strength);
                for (std::size_t i = 0; i < img.pixel_count(); ++i) {
                    const double m = strength * (0.35 + 0.65 * detail);
                    const double cm = colour * 0.8;
                    for (int c = 0; c < 3; ++c)
                        img.data()[i][c] = static_cast<float>(img.data()[i][c] * (1 - m) + soft.data()[i][c] * m);
                    const float l0 = lumaF(img.data()[i]);
                    const float l1 = lumaF(soft.data()[i]);
                    for (int c = 0; c < 3; ++c) {
                        const double chroma = static_cast<double>(img.data()[i][c]) - l0;
                        img.data()[i][c] = static_cast<float>(l0 + chroma * (1 - cm) + (l1 - l0 + (soft.data()[i][c] - l1)) * cm);
                    }
                }
                if (jpeg) {
                    Image med2 = img.clone();
                    medianR(med2, 2);
                    for (std::size_t i = 0; i < img.pixel_count(); ++i) {
                        for (int c = 0; c < 3; ++c)
                            img.data()[i][c] = static_cast<float>(img.data()[i][c] * 0.5 + med2.data()[i][c] * 0.5);
                    }
                }
                if (sharp > 0.01) unsharpF(img, sharp * 1.5, 1, 0.02);
            }
            return true;
        }
        medianR(img, 1);
        gaussBlur(img, 0.6);
        const double hl = std::clamp(p1, 0.0, 20.0) / 20.0;
        const double inten = std::clamp(p2v(par), 0.0, 10.0) / 10.0;
        if (p0 > 0.05) {
            std::uint64_t st = 12345;
            for (std::uint32_t y = 0; y < h; ++y) {
                for (std::uint32_t x = 0; x < w; ++x) {
                    const double g = (hash2i(static_cast<int>(x), static_cast<int>(y), st + x + y * 131) % 1000) / 1000.0 - 0.5;
                    RGBAf &t = img.at(x, y);
                    const double gate = 1.0 - hl * lumaF(t);
                    const double d = g * 0.06 * (p0 / 4.0) * inten * gate;
                    t.r = static_cast<float>(c01(t.r + d));
                    t.g = static_cast<float>(c01(t.g + d));
                    t.b = static_cast<float>(c01(t.b + d));
                }
            }
        }
        return true;
    }
    if (id == "add_noise") {
        const double amt = p0 / 100.0;
        const bool gauss = std::round(p1) > 0.5;
        const bool mono = p2v(par) > 0.5;
        std::uint64_t st = 0xC0FFEEULL;
        auto raw = [&](int x, int y, int c) {
            return (hash2i(x, y, st + static_cast<std::uint64_t>(c) * 101 + static_cast<std::uint64_t>(y) * 3) % 100000) / 100000.0 * 2.0 - 1.0;
        };
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                const int xi = static_cast<int>(x);
                const int yi = static_cast<int>(y);
                if (mono) {
                    double d = raw(xi, yi, 0);
                    if (gauss) d = (d + raw(xi ^ 0x5bd1e995, yi, 0) + raw(xi, yi ^ 0x1b873593, 0)) / 3.0 * 1.7;
                    d *= amt;
                    for (int c = 0; c < 3; ++c)
                        img.at(x, y)[c] = static_cast<float>(c01(static_cast<double>(img.at(x, y)[c]) + d));
                } else {
                    for (int c = 0; c < 3; ++c) {
                        double d = raw(xi, yi, c);
                        if (gauss) d = (d + raw(xi ^ 0x5bd1e995, yi, c) + raw(xi, yi ^ 0x1b873593, c)) / 3.0 * 1.7;
                        d *= amt;
                        img.at(x, y)[c] = static_cast<float>(c01(static_cast<double>(img.at(x, y)[c]) + d));
                    }
                }
            }
        }
        return true;
    }
    if (id == "grain") {
        const double amt = p0 / 100.0 * 0.25;
        const double contrast = 0.5 + p1 / 100.0;
        const int kind = std::clamp(static_cast<int>(std::round(p2v(par))), 0, 9);
        std::uint64_t st = 0xBADC0FFEEULL;
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                const int xi = static_cast<int>(x);
                const int yi = static_cast<int>(y);
                double d = 0;
                if (kind == 7) {
                    d = (hash2i(0, yi, st) % 100000) / 100000.0 * 2.0 - 1.0;
                } else if (kind == 8) {
                    d = (hash2i(xi, 0, st) % 100000) / 100000.0 * 2.0 - 1.0;
                } else if (kind == 1) {
                    d = ((hash2i(xi, yi, st) % 100000) / 100000.0 * 2.0 - 1.0 +
                         (hash2i(xi + 13, yi + 7, st) % 100000) / 100000.0 * 2.0 - 1.0 +
                         (hash2i(xi - 5, yi + 11, st) % 100000) / 100000.0 * 2.0 - 1.0) / 3.0;
                } else if (kind == 2 || kind == 9) {
                    const double u = (hash2i(xi, yi, st) % 100000) / 100000.0;
                    d = u > 0.93 ? (u - 0.93) * 14.0 : 0.0;
                } else if (kind == 3 || kind == 5 || kind == 6) {
                    const double sc = kind == 5 ? 0.15 : 0.45;
                    d = (vnoise(xi * sc, yi * sc, st) - 0.5) * 2.0;
                } else {
                    d = (hash2i(xi, yi, st) % 100000) / 100000.0 * 2.0 - 1.0;
                }
                if (kind == 4) d = (d >= 0 ? 1 : -1) * std::pow(std::fabs(d), 0.5);
                d *= amt * contrast;
                for (int c = 0; c < 3; ++c)
                    img.at(x, y)[c] = static_cast<float>(c01(static_cast<double>(img.at(x, y)[c]) + d));
            }
        }
        return true;
    }
    return false;
}

}  // namespace pittore::filter
