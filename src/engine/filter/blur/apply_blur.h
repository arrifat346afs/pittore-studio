#pragma once
// Blur filters. Split from engine/filter/filters.h.
#include <string>
#include <vector>

#include "engine/core/image.h"
#include "engine/filter/core/filter_detail.h"
#include "engine/filter/core/filter_params.h"

namespace pittore::filter {

inline bool applyBlur(Image& img, Image& scratch, const std::string& id,
                       const std::vector<double>& par) {
    using namespace detail;
    const std::uint32_t w = img.width();
    const std::uint32_t h = img.height();
    auto p0 = pv(par, 0, 0.0);
    auto p1 = pv(par, 1, 0.0);
    if (id == "average") {
        const double mix = std::clamp(pv(par, 0, 100.0) / 100.0, 0.0, 1.0);
        if (mix <= 0.0) return true;
        double ar = 0, ag = 0, ab = 0, aa = 0;
        double ur = 0, ug = 0, ub = 0;
        const RGBAf *px = img.data();
        const std::size_t n = img.pixel_count();
        for (std::size_t i = 0; i < n; ++i) {
            const double a = px[i].a;
            ar += px[i].r * a;
            ag += px[i].g * a;
            ab += px[i].b * a;
            aa += a;
            ur += px[i].r;
            ug += px[i].g;
            ub += px[i].b;
        }
        float fr, fg, fb;
        if (aa > 0.0) {
            fr = static_cast<float>(ar / aa);
            fg = static_cast<float>(ag / aa);
            fb = static_cast<float>(ab / aa);
        } else if (n > 0) {
            fr = static_cast<float>(ur / n);
            fg = static_cast<float>(ug / n);
            fb = static_cast<float>(ub / n);
        } else {
            return true;
        }
        for (std::size_t i = 0; i < n; ++i) {
            img.data()[i].r = static_cast<float>(img.data()[i].r * (1.0 - mix) + fr * mix);
            img.data()[i].g = static_cast<float>(img.data()[i].g * (1.0 - mix) + fg * mix);
            img.data()[i].b = static_cast<float>(img.data()[i].b * (1.0 - mix) + fb * mix);
        }
        return true;
    }
    if (id == "blur" || id == "blur_more") {
        const double m = strengthOf(par);
        if (m <= 0.0) return true;
        Image orig = img.clone();
        boxBlurInto(img, scratch, id == std::string("blur") ? 1 : 3);
        std::memcpy(img.data(), scratch.data(), sizeof(RGBAf) * img.pixel_count());
        blendInto(img, orig, m);
        return true;
    }
    if (id == "box_blur") {
        boxBlurInto(img, scratch, std::max(1, static_cast<int>(std::round(p0))));
        std::memcpy(img.data(), scratch.data(), sizeof(RGBAf) * img.pixel_count());
        return true;
    }
    if (id == "gaussian_blur") {
        gaussBlur(img, std::max(0.1, p0));
        return true;
    }
    if (id == "lens_blur") {
        const int r = std::max(1, static_cast<int>(std::round(std::clamp(p0, 0.0, 50.0))));
        const int shape = std::clamp(static_cast<int>(std::round(p1)), 0, 6);
        const double bright = std::clamp(p2v(par), 0.0, 100.0) / 100.0;
        const double thr = std::clamp(p3v(par), 0.0, 255.0) / 255.0;
        const double noise = std::clamp(p4v(par), 0.0, 50.0) / 50.0;
        const int kind = shape == 0 ? 0 : 10 + shape + 2;
        Image blur(w, h);
        shapedBlurInto(img, blur, r, kind);
        std::uint64_t st = 777;
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                const RGBAf &o = img.at(x, y);
                const RGBAf &b = blur.at(x, y);
                const double spec = std::clamp((static_cast<double>(lumaF(o)) - thr) * 4.0 + 0.5, 0.0, 1.0);
                const double m = 0.35 + 0.65 * spec;
                RGBAf &d = img.at(x, y);
                d.r = static_cast<float>(o.r * (1 - m) + b.r * m + spec * bright * 0.4);
                d.g = static_cast<float>(o.g * (1 - m) + b.g * m + spec * bright * 0.4);
                d.b = static_cast<float>(o.b * (1 - m) + b.b * m + spec * bright * 0.4);
                if (noise > 0.01) {
                    const double g = (hash2i(static_cast<int>(x), static_cast<int>(y), st) % 1000) / 1000.0 - 0.5;
                    d.r = static_cast<float>(c01(d.r + g * 0.08 * noise));
                    d.g = static_cast<float>(c01(d.g + g * 0.08 * noise));
                    d.b = static_cast<float>(c01(d.b + g * 0.08 * noise));
                }
            }
        }
        return true;
    }
    if (id == "motion_blur") {
        const double ang = p0 * 3.141592653589793 / 180.0;
        const int dist = std::max(1, static_cast<int>(std::round(p1)));
        const double dx = std::cos(ang);
        const double dy = std::sin(ang);
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                double ar = 0, ag = 0, ab = 0;
                for (int i = -dist; i <= dist; ++i) {
                    RGBAf s = bilin(img, x + dx * i, y + dy * i);
                    ar += s.r;
                    ag += s.g;
                    ab += s.b;
                }
                const double n = 2 * dist + 1;
                RGBAf &d = scratch.at(x, y);
                d.r = static_cast<float>(ar / n);
                d.g = static_cast<float>(ag / n);
                d.b = static_cast<float>(ab / n);
                d.a = img.at(x, y).a;
            }
        }
        std::memcpy(img.data(), scratch.data(), sizeof(RGBAf) * img.pixel_count());
        return true;
    }
    if (id == "radial_blur") {
        const double amt = std::clamp(p0, 1.0, 100.0) / 100.0;
        const bool spin = std::round(p1) < 0.5;
        const int qi = std::clamp(static_cast<int>(std::round(p2v(par))), 0, 2);
        const double cx = w * std::clamp(p3v(par), 0.0, 100.0) / 100.0;
        const double cy = h * std::clamp(p4v(par), 0.0, 100.0) / 100.0;
        const int steps = qi == 0 ? 4 : (qi == 1 ? 8 : 16);
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                double ar = 0, ag = 0, ab = 0;
                for (int i = 0; i < steps; ++i) {
                    const double t = amt * i / steps;
                    double sx = x;
                    double sy = y;
                    if (spin) {
                        const double a = t * 0.5;
                        const double ox = x - cx;
                        const double oy = y - cy;
                        sx = cx + ox * std::cos(a) - oy * std::sin(a);
                        sy = cy + ox * std::sin(a) + oy * std::cos(a);
                    } else {
                        sx = cx + (x - cx) * (1.0 - t * 0.25);
                        sy = cy + (y - cy) * (1.0 - t * 0.25);
                    }
                    RGBAf s = bilin(img, sx, sy);
                    ar += s.r;
                    ag += s.g;
                    ab += s.b;
                }
                RGBAf &d = scratch.at(x, y);
                d.r = static_cast<float>(ar / steps);
                d.g = static_cast<float>(ag / steps);
                d.b = static_cast<float>(ab / steps);
                d.a = img.at(x, y).a;
            }
        }
        std::memcpy(img.data(), scratch.data(), sizeof(RGBAf) * img.pixel_count());
        return true;
    }
    if (id == "shape_blur") {
        const int r = std::max(1, static_cast<int>(std::round(p0)));
        const int shape = std::clamp(static_cast<int>(std::round(p1)), 0, 5);
        shapedBlurInto(img, scratch, r, shape);
        std::memcpy(img.data(), scratch.data(), sizeof(RGBAf) * img.pixel_count());
        return true;
    }
    if (id == "smart_blur" || id == "surface_blur") {
        const int r = std::max(1, static_cast<int>(std::round(p0)));
        const double thr = id == std::string("surface_blur") ? std::clamp(p1, 1.0, 255.0) / 255.0 : std::clamp(p1, 0.05, 100.0) / 100.0;
        const int mode = id == std::string("smart_blur") ? std::clamp(static_cast<int>(std::round(p2v(par))), 0, 2) : 0;
        Image blur(w, h);
        boxBlurInto(img, blur, r);
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                const RGBAf &o = img.at(x, y);
                const RGBAf &b = blur.at(x, y);
                const double d0 = std::fabs(o.r - b.r) + std::fabs(o.g - b.g) + std::fabs(o.b - b.b);
                const double m = std::clamp(d0 / (3.0 * thr + 1e-6), 0.0, 1.0);
                RGBAf &d = img.at(x, y);
                const double kr = o.r * m + b.r * (1 - m);
                const double kg = o.g * m + b.g * (1 - m);
                const double kb = o.b * m + b.b * (1 - m);
                if (mode == 1) {
                    const double e = m > 0.5 ? 1.0 : 0.0;
                    d.r = d.g = d.b = static_cast<float>(e);
                } else if (mode == 2) {
                    d.r = static_cast<float>(c01(kr + m * 0.5));
                    d.g = static_cast<float>(c01(kg + m * 0.5));
                    d.b = static_cast<float>(c01(kb + m * 0.5));
                } else {
                    d.r = static_cast<float>(kr);
                    d.g = static_cast<float>(kg);
                    d.b = static_cast<float>(kb);
                }
            }
        }
        return true;
    }
    return false;
}

}  // namespace pittore::filter
