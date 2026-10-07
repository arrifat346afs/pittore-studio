#pragma once
// Blur Gallery filters. Split from engine/filter/filters.h.
#include <string>
#include <vector>

#include "engine/core/image.h"
#include "engine/filter/core/filter_detail.h"
#include "engine/filter/core/filter_params.h"

namespace pittore::filter {

inline bool applyBlurGallery(Image& img, Image& scratch, const std::string& id,
                       const std::vector<double>& par) {
    using namespace detail;
    const std::uint32_t w = img.width();
    const std::uint32_t h = img.height();
    auto p0 = pv(par, 0, 0.0);
    auto p1 = pv(par, 1, 0.0);
    if (id == "field_blur") {
        const double r = std::clamp(p0, 0.0, 200.0) * 0.25;
        const double pos = std::clamp(p1, 0.0, 100.0) / 100.0;
        const double spread = std::max(0.02, std::clamp(p2v(par), 1.0, 100.0) / 100.0);
        Image blur(w, h);
        boxBlurInto(img, blur, std::max(1, static_cast<int>(std::round(r))));
        for (std::uint32_t y = 0; y < h; ++y) {
            const double m = std::clamp(std::fabs(static_cast<double>(y) / h - pos) / spread, 0.0, 1.0);
            for (std::uint32_t x = 0; x < w; ++x) {
                const RGBAf &o = img.at(x, y);
                const RGBAf &b = blur.at(x, y);
                RGBAf &t = img.at(x, y);
                t.r = static_cast<float>(o.r * (1 - m) + b.r * m);
                t.g = static_cast<float>(o.g * (1 - m) + b.g * m);
                t.b = static_cast<float>(o.b * (1 - m) + b.b * m);
            }
        }
        return true;
    }
    if (id == "iris_blur") {
        const double r = std::clamp(p0, 0.0, 200.0) * 0.25;
        const double cx = w * std::clamp(p1, 0.0, 100.0) / 100.0;
        const double cy = h * std::clamp(p2v(par), 0.0, 100.0) / 100.0;
        const double rad = std::min(w, h) * std::clamp(p3v(par), 1.0, 100.0) / 100.0;
        const double round = std::clamp(p4v(par), 0.0, 100.0) / 100.0;
        const double feather = std::max(0.02, std::clamp(p5v(par), 0.0, 100.0) / 100.0);
        const int shape = std::clamp(static_cast<int>(std::round(pv(par, 6, 0.0))), 0, 6);
        Image blur(w, h);
        boxBlurInto(img, blur, std::max(1, static_cast<int>(std::round(r))));
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                double dx = static_cast<double>(x) - cx;
                double dy = (static_cast<double>(y) - cy) * (2.0 - round);
                double d = std::hypot(dx, dy);
                if (shape > 0) {
                    const double th = std::atan2(dy, dx);
                    const double sc = detail::polyScale(th, shape + 2);
                    d /= std::max(0.2, sc);
                }
                const double m = std::clamp((d - rad) / (rad * feather + 1.0), 0.0, 1.0);
                const RGBAf &o = img.at(x, y);
                const RGBAf &b = blur.at(x, y);
                RGBAf &t = img.at(x, y);
                t.r = static_cast<float>(o.r * (1 - m) + b.r * m);
                t.g = static_cast<float>(o.g * (1 - m) + b.g * m);
                t.b = static_cast<float>(o.b * (1 - m) + b.b * m);
            }
        }
        return true;
    }
    if (id == "tilt_shift") {
        const double r = std::clamp(p0, 0.0, 200.0) * 0.25;
        const double pos = std::clamp(p1, 0.0, 100.0) / 100.0;
        const double band = std::max(0.02, std::clamp(p2v(par), 1.0, 100.0) / 100.0);
        const double feather = std::max(0.02, std::clamp(p3v(par), 0.0, 100.0) / 100.0);
        const double ang = p4v(par) * 3.141592653589793 / 180.0;
        const double ca = std::cos(ang);
        const double sa = std::sin(ang);
        Image blur(w, h);
        boxBlurInto(img, blur, std::max(1, static_cast<int>(std::round(r))));
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                const double lx = (static_cast<double>(x) / w - 0.5) * ca + (static_cast<double>(y) / h - 0.5) * sa + 0.5;
                const double d = std::fabs(lx - pos) / band;
                const double m = std::clamp((d - (1.0 - feather)) / (feather + 1e-6), 0.0, 1.0);
                const RGBAf &o = img.at(x, y);
                const RGBAf &b = blur.at(x, y);
                RGBAf &t = img.at(x, y);
                t.r = static_cast<float>(o.r * (1 - m) + b.r * m);
                t.g = static_cast<float>(o.g * (1 - m) + b.g * m);
                t.b = static_cast<float>(o.b * (1 - m) + b.b * m);
            }
        }
        return true;
    }
    if (id == "spin_blur") {
        const double angMax = p0 * 3.141592653589793 / 180.0;
        const double cx = w * std::clamp(p1, 0.0, 100.0) / 100.0;
        const double cy = h * std::clamp(p2v(par), 0.0, 100.0) / 100.0;
        const double rad = std::min(w, h) * std::clamp(p3v(par), 1.0, 100.0) / 100.0;
        const double feather = std::max(0.02, std::clamp(p4v(par), 0.0, 100.0) / 100.0);
        Image spun(w, h);
        std::memcpy(spun.data(), img.data(), sizeof(RGBAf) * img.pixel_count());
        {
            const int steps = 10;
            for (std::uint32_t y = 0; y < h; ++y) {
                for (std::uint32_t x = 0; x < w; ++x) {
                    double ar = 0, ag = 0, ab = 0;
                    for (int i = 0; i < steps; ++i) {
                        const double a = angMax * i / steps;
                        const double ox = static_cast<double>(x) - cx;
                        const double oy = static_cast<double>(y) - cy;
                        RGBAf s = bilin(img, cx + ox * std::cos(a) - oy * std::sin(a),
                                        cy + ox * std::sin(a) + oy * std::cos(a));
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
        }
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                const double d = std::hypot(static_cast<double>(x) - cx, static_cast<double>(y) - cy);
                const double m = std::clamp((d - rad) / (rad * feather + 1.0), 0.0, 1.0);
                const RGBAf &o = img.at(x, y);
                const RGBAf &b = scratch.at(x, y);
                RGBAf &t = img.at(x, y);
                t.r = static_cast<float>(o.r * m + b.r * (1 - m));
                t.g = static_cast<float>(o.g * m + b.g * (1 - m));
                t.b = static_cast<float>(o.b * m + b.b * (1 - m));
            }
        }
        return true;
    }
    if (id == "path_blur") {
        const double speed = std::clamp(p0, 0.0, 100.0) / 100.0;
        const double ang = p1 * 3.141592653589793 / 180.0;
        const double curve = p2v(par) / 100.0;
        const double taper = std::clamp(p3v(par), 0.0, 100.0) / 100.0;
        const double dx = std::cos(ang);
        const double dy = std::sin(ang);
        const int dist = std::max(1, static_cast<int>(std::round(speed * 24.0)));
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                double ar = 0, ag = 0, ab = 0, wsum = 0;
                for (int i = -dist; i <= dist; ++i) {
                    const double t = static_cast<double>(i) / dist;
                    const double tw = 1.0 - taper * std::fabs(t);
                    const double bend = curve * 8.0 * t * t;
                    RGBAf s = bilin(img, static_cast<double>(x) + dx * i - dy * bend,
                                    static_cast<double>(y) + dy * i + dx * bend);
                    ar += s.r * tw;
                    ag += s.g * tw;
                    ab += s.b * tw;
                    wsum += tw;
                }
                wsum = std::max(1e-6, wsum);
                RGBAf &d = scratch.at(x, y);
                d.r = static_cast<float>(ar / wsum);
                d.g = static_cast<float>(ag / wsum);
                d.b = static_cast<float>(ab / wsum);
                d.a = img.at(x, y).a;
            }
        }
        std::memcpy(img.data(), scratch.data(), sizeof(RGBAf) * img.pixel_count());
        return true;
    }
    return false;
}

}  // namespace pittore::filter
