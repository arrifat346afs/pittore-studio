#pragma once
// Other filters. Split from engine/filter/filters.h.
#include <string>
#include <vector>

#include "engine/core/image.h"
#include "engine/filter/core/filter_detail.h"
#include "engine/filter/core/filter_params.h"

namespace pittore::filter {

inline bool applyOther(Image& img, Image& scratch, const std::string& id,
                       const std::vector<double>& par) {
    using namespace detail;
    const std::uint32_t w = img.width();
    const std::uint32_t h = img.height();
    auto p0 = pv(par, 0, 0.0);
    auto p1 = pv(par, 1, 0.0);
    if (id == "high_pass") {
        Image blur(w, h);
        boxBlurInto(img, blur, std::max(1, static_cast<int>(std::round(p0))));
        for (std::size_t i = 0; i < img.pixel_count(); ++i) {
            for (int c = 0; c < 3; ++c)
                img.data()[i][c] = static_cast<float>(c01(0.5 + (img.data()[i][c] - blur.data()[i][c])));
        }
        return true;
    }
    if (id == "maximum" || id == "minimum") {
        const int r = std::max(1, static_cast<int>(std::round(p0)));
        const bool round = std::round(p1) > 0.5;
        const bool isMax = id == std::string("maximum");
        Image out(w, h);
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                RGBAf acc = img.at(x, y);
                for (int c = 0; c < 3; ++c) {
                    float v = acc[c];
                    for (int dy = -r; dy <= r; ++dy) {
                        for (int dx = -r; dx <= r; ++dx) {
                            if (round && dx * dx + dy * dy > r * r) continue;
                            const float s = img.at(cw(img, static_cast<int>(x) + dx), ch(img, static_cast<int>(y) + dy))[c];
                            v = isMax ? std::max(v, s) : std::min(v, s);
                        }
                    }
                    acc[c] = v;
                }
                out.at(x, y) = acc;
            }
        }
        std::memcpy(img.data(), out.data(), sizeof(RGBAf) * img.pixel_count());
        return true;
    }
    if (id == "offset") {
        const int ox = static_cast<int>(std::round(p0));
        const int oy = static_cast<int>(std::round(p1));
        const int undef = std::clamp(static_cast<int>(std::round(p2v(par))), 0, 2);
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                const int sx = static_cast<int>(x) - ox;
                const int sy = static_cast<int>(y) - oy;
                if (undef == 0 && (sx < 0 || sy < 0 || sx >= static_cast<int>(w) || sy >= static_cast<int>(h))) {
                    scratch.at(x, y) = RGBAf{0, 0, 0, 0};
                } else if (undef == 2) {
                    const int wx = ((sx % static_cast<int>(w)) + w) % w;
                    const int wy = ((sy % static_cast<int>(h)) + h) % h;
                    scratch.at(x, y) = img.at(static_cast<std::uint32_t>(wx), static_cast<std::uint32_t>(wy));
                } else {
                    scratch.at(x, y) = img.at(cw(img, sx), ch(img, sy));
                }
            }
        }
        std::memcpy(img.data(), scratch.data(), sizeof(RGBAf) * img.pixel_count());
        return true;
    }
    if (id == "custom") {
        const double c = std::clamp(p0, 1.0, 9.0);
        const double sc = std::clamp(p1, 0.1, 10.0);
        const double off = std::clamp(p2v(par), -255.0, 255.0) / 255.0;
        Image blur(w, h);
        boxBlurInto(img, blur, 1);
        for (std::size_t i = 0; i < img.pixel_count(); ++i) {
            for (int chh = 0; chh < 3; ++chh)
                img.data()[i][chh] = static_cast<float>(c01((img.data()[i][chh] * c - blur.data()[i][chh] * (c - 1.0)) / sc + off));
        }
        return true;
    }
    if (id == "hsb_hsl") {
        const int m = static_cast<int>(std::round(p0)) % 4;
        for (std::size_t i = 0; i < img.pixel_count(); ++i) {
            float h, s, l;
            toHsl(img.data()[i].r, img.data()[i].g, img.data()[i].b, h, s, l);
            if (m == 0)
                l = std::pow(l, 0.8f);
            else if (m == 1)
                l = std::pow(l, 1.25f);
            else if (m == 2)
                s = std::clamp(s * 1.2f, 0.0f, 1.0f);
            else
                s = std::clamp(s * 0.85f, 0.0f, 1.0f);
            fromHsl(h, s, l, img.data()[i].r, img.data()[i].g, img.data()[i].b);
        }
        return true;
    }
    return false;
}

}  // namespace pittore::filter
