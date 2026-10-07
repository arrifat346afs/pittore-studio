#pragma once
// Distort filters. Split from engine/filter/filters.h.
#include <string>
#include <vector>

#include "engine/core/image.h"
#include "engine/filter/core/filter_detail.h"
#include "engine/filter/core/filter_params.h"

namespace pittore::filter {

inline bool applyDistort(Image& img, Image& scratch, const std::string& id,
                       const std::vector<double>& par) {
    using namespace detail;
    const std::uint32_t w = img.width();
    const std::uint32_t h = img.height();
    auto p0 = pv(par, 0, 0.0);
    auto p1 = pv(par, 1, 0.0);
    if (id == "pinch" || id == "spherize") {
        const double amt = p0 / 100.0;
        const int mode = std::clamp(static_cast<int>(std::round(p1)), 0, 2);
        remapWarp(img, scratch, [&](double x, double y, double ww, double hh) {
            double nx = (x / ww) * 2 - 1;
            double ny = (y / hh) * 2 - 1;
            if (mode == 1) ny = 0;
            if (mode == 2) nx = 0;
            const double r = std::sqrt(nx * nx + ny * ny);
            if (r > 1e-6 && r <= 1.0) {
                const double f = 1.0 - amt * (1.0 - r * r) * 0.5;
                if (mode != 2) nx *= f;
                if (mode != 1) ny *= f;
            }
            double ox = (nx + 1) * 0.5 * ww;
            double oy = (ny + 1) * 0.5 * hh;
            if (mode == 1) oy = y;
            if (mode == 2) ox = x;
            return std::make_pair(ox, oy);
        });
        return true;
    }
    if (id == "twirl") {
        const double ang = p0 * 3.141592653589793 / 180.0;
        remapWarp(img, scratch, [&](double x, double y, double ww, double hh) {
            const double cx = ww * 0.5;
            const double cy = hh * 0.5;
            const double dx = x - cx;
            const double dy = y - cy;
            const double r = std::sqrt(dx * dx + dy * dy) / (std::min(ww, hh) * 0.5);
            const double a = std::atan2(dy, dx) + ang * std::max(0.0, 1.0 - r);
            const double rr = std::sqrt(dx * dx + dy * dy);
            return std::make_pair(cx + rr * std::cos(a), cy + rr * std::sin(a));
        });
        return true;
    }
    if (id == "ripple") {
        const double amt = p0 / 100.0 * 8.0;
        const double sz = std::max(1.0, p1);
        remapWarp(img, scratch, [&](double x, double y, double ww, double hh) {
            (void)ww;
            (void)hh;
            return std::make_pair(x + std::sin(y / sz * 6.2831) * amt, y + std::cos(x / sz * 6.2831) * amt);
        });
        return true;
    }
    if (id == "wave") {
        const int gens = std::clamp(static_cast<int>(std::round(p0)), 1, 8);
        const double wl = std::max(2.0, p1);
        const double amp = p2v(par);
        const double hs = std::clamp(p3v(par), 0.0, 100.0) / 100.0;
        const double vs = std::clamp(p4v(par), 0.0, 100.0) / 100.0;
        const int type = std::clamp(static_cast<int>(std::round(p5v(par))), 0, 2);
        const double sseed = std::max(0.0, p6v(par));
        auto wavefn = [&](double ph) {
            if (type == 1) {
                double t = std::fmod(ph / 6.283185307179586, 1.0);
                if (t < 0) t += 1.0;
                return std::fabs(t - 0.5) * 4.0 - 1.0;
            }
            if (type == 2) return (std::sin(ph) >= 0 ? 1.0 : -1.0);
            return std::sin(ph);
        };
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                double ox = 0, oy = 0;
                for (int g = 0; g < gens; ++g) {
                    const double f = 1.0 + g * 0.37;
                    const double ph = (x / (wl * f) + y / (wl * f * 1.7) + g * 1.3 + sseed * 0.01) * 6.283185307179586;
                    ox += wavefn(ph) * amp / gens * hs;
                    oy += wavefn(ph * 1.31 + 2.0) * amp / gens * vs;
                }
                scratch.at(x, y) = bilin(img, static_cast<double>(x) + ox, static_cast<double>(y) + oy);
            }
        }
        std::memcpy(img.data(), scratch.data(), sizeof(RGBAf) * img.pixel_count());
        return true;
    }
    if (id == "zigzag") {
        const double amt = p0 / 100.0 * 20.0;
        const double ridges = std::max(1.0, p1);
        const int style = std::clamp(static_cast<int>(std::round(p2v(par))), 0, 2);
        remapWarp(img, scratch, [&](double x, double y, double ww, double hh) {
            const double cx = ww * 0.5;
            const double cy = hh * 0.5;
            const double dx = x - cx;
            const double dy = y - cy;
            const double r = std::sqrt(dx * dx + dy * dy) + 1e-6;
            const double mn = std::min(ww, hh);
            const double w = std::sin(r / mn * ridges * 6.2831) * amt * (r / mn);
            if (style == 0) {
                return std::make_pair(x - dy / r * w, y + dx / r * w);
            }
            if (style == 2) {
                const double w2 = std::sin(r / mn * ridges * 6.2831) * amt * 0.5;
                return std::make_pair(x + dx / r * w2, y + dy / r * w2);
            }
            return std::make_pair(x + dx / r * w, y + dy / r * w);
        });
        return true;
    }
    if (id == "shear") {
        const double c = p0;
        const int curve = std::clamp(static_cast<int>(std::round(p1)), 0, 2);
        const bool wrap = std::round(p2v(par)) > 0.5;
        for (std::uint32_t y = 0; y < h; ++y) {
            const double t = h > 1 ? static_cast<double>(y) / (h - 1) : 0.0;
            double s = 0;
            if (curve == 0) s = std::sin(t * 3.141592653589793);
            else if (curve == 1) s = std::sin(t * 6.283185307179586);
            else s = t * 2.0 - 1.0;
            for (std::uint32_t x = 0; x < w; ++x) {
                double sx = static_cast<double>(x) + s * c;
                if (wrap) {
                    sx = std::fmod(sx, static_cast<double>(w));
                    if (sx < 0) sx += w;
                    scratch.at(x, y) = bilin(img, sx, static_cast<double>(y));
                } else {
                    if (sx < 0 || sx > w - 1) {
                        scratch.at(x, y) = RGBAf{0, 0, 0, 0};
                    } else {
                        scratch.at(x, y) = bilin(img, sx, static_cast<double>(y));
                    }
                }
            }
        }
        std::memcpy(img.data(), scratch.data(), sizeof(RGBAf) * img.pixel_count());
        return true;
    }
    if (id == "polar") {
        const bool toP = p0 > 0.5;
        remapWarp(img, scratch, [&](double x, double y, double ww, double hh) {
            const double cx = ww * 0.5;
            const double cy = hh * 0.5;
            if (toP) {
                const double dx = x - cx;
                const double dy = y - cy;
                const double r = std::sqrt(dx * dx + dy * dy) / (std::min(ww, hh) * 0.5);
                double a = std::atan2(dy, dx) / 6.2831853 + 0.5;
                return std::make_pair(a * ww, r * hh);
            }
            const double a = (x / ww - 0.5) * 6.2831853;
            const double r = (y / hh) * std::min(ww, hh) * 0.5;
            return std::make_pair(cx + r * std::cos(a), cy + r * std::sin(a));
        });
        return true;
    }
    if (id == "displace") {
        const double hs = std::clamp(p0, 0.0, 100.0) / 100.0 * 32.0;
        const double vs = std::clamp(p1, 0.0, 100.0) / 100.0 * 32.0;
        const bool tile = std::round(p2v(par)) > 0.5;
        const bool wrap = std::round(p3v(par)) > 0.5;
        Image disp(w, h);
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x)
                disp.at(x, y).r = disp.at(x, y).g = static_cast<float>(fbm(x * 0.02, y * 0.02, 4, 77));
        }
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                double dx = (disp.at(x, y).r - 0.5) * 2.0 * hs;
                double dy = (disp.at(x, y).g - 0.5) * 2.0 * vs;
                if (tile) {
                    dx = std::fmod(dx, static_cast<double>(w));
                    dy = std::fmod(dy, static_cast<double>(h));
                }
                double sx = static_cast<double>(x) + dx;
                double sy = static_cast<double>(y) + dy;
                if (wrap) {
                    sx = std::fmod(sx, static_cast<double>(w));
                    if (sx < 0) sx += w;
                    sy = std::fmod(sy, static_cast<double>(h));
                    if (sy < 0) sy += h;
                    scratch.at(x, y) = bilin(img, sx, sy);
                } else {
                    scratch.at(x, y) = bilin(img, sx, sy);
                }
            }
        }
        std::memcpy(img.data(), scratch.data(), sizeof(RGBAf) * img.pixel_count());
        return true;
    }
    if (id == "glass") {
        const double amt = p0;
        const double smooth = std::clamp(p1, 1.0, 15.0);
        const int tex = std::clamp(static_cast<int>(std::round(p2v(par))), 0, 5);
        const double scaling = std::clamp(p3v(par), 50.0, 200.0) / 100.0;
        const double sz = (tex == 1 ? 9.0 : 4.0) * scaling;
        Image pre = img.clone();
        if (smooth > 1.5) {
            boxBlurInto(pre, scratch, static_cast<int>(std::round(smooth * 0.5)));
            std::memcpy(pre.data(), scratch.data(), sizeof(RGBAf) * img.pixel_count());
        }
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                const double dx = (vnoise(x / sz, y / sz, 31) - 0.5) * 2.0 * amt;
                const double dy = (vnoise(x / sz + 50, y / sz + 50, 77) - 0.5) * 2.0 * amt;
                scratch.at(x, y) = bilin(pre, static_cast<double>(x) + dx, static_cast<double>(y) + dy);
            }
        }
        std::memcpy(img.data(), scratch.data(), sizeof(RGBAf) * img.pixel_count());
        return true;
    }
    if (id == "ocean_ripple") {
        const double sz = std::max(1.0, p0);
        const double amt = p1;
        remapWarp(img, scratch, [&](double x, double y, double ww, double hh) {
            (void)ww;
            (void)hh;
            const double dx = (vnoise(x / sz, y / sz, 31) - 0.5) * 2.0 * amt;
            const double dy = (vnoise(x / sz + 50, y / sz + 50, 77) - 0.5) * 2.0 * amt;
            return std::make_pair(x + dx, y + dy);
        });
        return true;
    }
    return false;
}

}  // namespace pittore::filter
