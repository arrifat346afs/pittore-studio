#pragma once
// Stylize filters. Split from engine/filter/filters.h.
#include <string>
#include <vector>

#include "engine/core/image.h"
#include "engine/filter/core/filter_detail.h"
#include "engine/filter/core/filter_params.h"

namespace pittore::filter {

inline bool applyStylize(Image& img, Image& scratch, const std::string& id,
                       const std::vector<double>& par) {
    using namespace detail;
    const std::uint32_t w = img.width();
    const std::uint32_t h = img.height();
    auto p0 = pv(par, 0, 0.0);
    auto p1 = pv(par, 1, 0.0);
    if (id == "solarize") {
        const double m = strengthOf(par);
        if (m <= 0.0) return true;
        Image orig = img.clone();
        for (std::size_t i = 0; i < img.pixel_count(); ++i) {
            for (int c = 0; c < 3; ++c) {
                const double v = img.data()[i][c];
                img.data()[i][c] = static_cast<float>(v > 0.5 ? 1.0 - v : v);
            }
        }
        blendInto(img, orig, m);
        return true;
    }
    if (id == "find_edges" || id == "glowing_edges" || id == "accented_edges") {
        const bool isFind = id == std::string("find_edges");
        const double m = isFind ? strengthOf(par) : 1.0;
        if (m <= 0.0) return true;
        Image orig = isFind ? img.clone() : Image(1, 1);
        const double wdt = isFind ? 1.0 : p0;
        const double bright = id == std::string("find_edges") ? 1.0 : (id == std::string("glowing_edges") ? std::clamp(p1, 0.0, 20.0) / 6.0 : std::clamp(p1, 0.0, 50.0) / 38.0);
        const double smooth = id == std::string("find_edges") ? 0.0 : std::clamp(p2v(par), 1.0, 15.0);
        Image pre = img.clone();
        if (smooth > 1.0) {
            boxBlurInto(pre, scratch, static_cast<int>(std::round(smooth * 0.5)));
            std::memcpy(pre.data(), scratch.data(), sizeof(RGBAf) * img.pixel_count());
        }
        Image out(w, h);
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                const double e = edgeMag(pre, x, y) * wdt;
                RGBAf &t = out.at(x, y);
                if (id == std::string("find_edges")) {
                    const double v = c01(1.0 - e * 2.0);
                    t.r = t.g = t.b = static_cast<float>(v);
                } else {
                    const RGBAf &o = img.at(x, y);
                    const double m = c01(e * 2.0 * bright);
                    t.r = static_cast<float>(o.r * (1 - m) + m);
                    t.g = static_cast<float>(o.g * (1 - m) + m * 0.9);
                    t.b = static_cast<float>(o.b * (1 - m) + m * 0.7);
                }
                t.a = img.at(x, y).a;
            }
        }
        std::memcpy(img.data(), out.data(), sizeof(RGBAf) * img.pixel_count());
        blendInto(img, orig, m);
        return true;
    }
    if (id == "emboss") {
        const double ang = p0 * 3.14159265 / 180.0;
        const double dx = std::cos(ang);
        const double dy = std::sin(ang);
        const double amt = p1;
        const double mix = std::clamp(p2v(par), 1.0, 500.0) / 100.0;
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                const double l0 = lumaF(bilin(img, static_cast<double>(x) - dx * amt, static_cast<double>(y) - dy * amt));
                const double l1 = lumaF(bilin(img, static_cast<double>(x) + dx * amt, static_cast<double>(y) + dy * amt));
                const double v = c01(0.5 + (l0 - l1) * 1.5);
                const RGBAf &o = img.at(x, y);
                RGBAf &t = scratch.at(x, y);
                const double k = std::clamp(mix, 0.0, 2.0) * 0.5;
                const double kk = std::clamp(k, 0.0, 1.0);
                t.r = static_cast<float>(o.r * (1 - kk) + v * kk);
                t.g = static_cast<float>(o.g * (1 - kk) + v * kk);
                t.b = static_cast<float>(o.b * (1 - kk) + v * kk);
                t.a = o.a;
            }
        }
        std::memcpy(img.data(), scratch.data(), sizeof(RGBAf) * img.pixel_count());
        return true;
    }
    if (id == "trace_contour") {
        const double lvl = std::clamp(p0, 0.0, 255.0) / 255.0;
        const bool upper = std::round(p1) > 0.5;
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                const double e = edgeMag(img, x, y);
                const double l = lumaF(img.at(x, y));
                const bool near = upper ? (l >= lvl && l < lvl + 0.25) : (l <= lvl && l > lvl - 0.25);
                const double v = (e > 0.08 && near) ? 0.0 : 1.0;
                RGBAf &t = img.at(x, y);
                t.r = t.g = t.b = static_cast<float>(v);
            }
        }
        return true;
    }
    if (id == "diffuse") {
        const double amt = p0;
        const int mode = std::clamp(static_cast<int>(std::round(p1)), 0, 3);
        Image moved(w, h);
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                double dx = 0, dy = 0;
                if (mode == 3) {
                    dx = (vnoise(x * 0.11, y * 0.11, 5) - 0.5) * 2.0 * amt;
                    dy = (vnoise(x * 0.11, y * 0.11, 5) - 0.5) * 2.0 * amt * 0.25;
                } else {
                    dx = (vnoise(x * 0.5, y * 0.5, 5) - 0.5) * 2.0 * amt;
                    dy = (vnoise(x * 0.5 + 9, y * 0.5 + 9, 6) - 0.5) * 2.0 * amt;
                }
                moved.at(x, y) = bilin(img, static_cast<double>(x) + dx, static_cast<double>(y) + dy);
            }
        }
        for (std::size_t i = 0; i < img.pixel_count(); ++i) {
            for (int c = 0; c < 3; ++c) {
                if (mode == 1) img.data()[i][c] = std::min(img.data()[i][c], moved.data()[i][c]);
                else if (mode == 2) img.data()[i][c] = std::max(img.data()[i][c], moved.data()[i][c]);
                else img.data()[i][c] = moved.data()[i][c];
            }
        }
        return true;
    }
    if (id == "diffuse_glow") {
        const double grain = p0;
        const double glow = p1 / 20.0;
        const double clear = p2v(par) / 20.0;
        Image blur(w, h);
        boxBlurInto(img, blur, std::max(1, static_cast<int>(std::round(grain)) + 1));
        std::uint64_t st = 4242;
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                RGBAf &t = img.at(x, y);
                const RGBAf &b = blur.at(x, y);
                const double g = (hash2i(static_cast<int>(x), static_cast<int>(y), st) % 1000) / 1000.0 - 0.5;
                for (int c = 0; c < 3; ++c)
                    t[c] = static_cast<float>(c01(t[c] * (1 - clear * 0.3) + b[c] * glow * 0.5 + 0.05 * glow + g * grain * 0.02));
            }
        }
        return true;
    }
    if (id == "wind") {
        const int base = std::max(1, static_cast<int>(std::round(p0)));
        const int method = std::clamp(static_cast<int>(std::round(p1)), 0, 2);
        const bool fromRight = std::round(p2v(par)) > 0.5;
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                int len = base;
                if (method == 1) len = base * 2;
                if (method == 2 && ((x / 8 + y / 8) % 2 == 0)) len = base / 2 + 1;
                double ar = 0, ag = 0, ab = 0;
                int n = 0;
                for (int i = 0; i < len; ++i) {
                    const int sx = fromRight ? static_cast<int>(x) + i : static_cast<int>(x) - i;
                    const RGBAf &s = img.at(cw(img, sx), y);
                    ar += s.r;
                    ag += s.g;
                    ab += s.b;
                    ++n;
                }
                RGBAf &t = scratch.at(x, y);
                t.r = static_cast<float>(ar / n);
                t.g = static_cast<float>(ag / n);
                t.b = static_cast<float>(ab / n);
                t.a = img.at(x, y).a;
            }
        }
        std::memcpy(img.data(), scratch.data(), sizeof(RGBAf) * img.pixel_count());
        return true;
    }
    if (id == "tiles") {
        const int n = std::max(2, static_cast<int>(std::round(p0)));
        const double maxOff = std::clamp(p1, 0.0, 100.0) / 100.0;
        const int fill = std::clamp(static_cast<int>(std::round(p2v(par))), 0, 4);
        const std::uint32_t cw2 = std::max<std::uint32_t>(1, w / n);
        const std::uint32_t ch2 = std::max<std::uint32_t>(1, h / n);
        std::uint64_t st = 9001;
        for (std::uint32_t ty = 0; ty * ch2 < h; ++ty) {
            for (std::uint32_t tx = 0; tx * cw2 < w; ++tx) {
                const double u1 = (hash2i(static_cast<int>(tx), static_cast<int>(ty), st) % 1000) / 1000.0 - 0.5;
                const double u2 = (hash2i(static_cast<int>(tx), static_cast<int>(ty), st + 7) % 1000) / 1000.0 - 0.5;
                const int ox = static_cast<int>(std::round(u1 * 2.0 * maxOff * cw2));
                const int oy = static_cast<int>(std::round(u2 * 2.0 * maxOff * ch2));
                for (std::uint32_t y = ty * ch2; y < std::min((ty + 1) * ch2, h); ++y) {
                    for (std::uint32_t x = tx * cw2; x < std::min((tx + 1) * cw2, w); ++x) {
                        const int sx = static_cast<int>(x) + ox;
                        const int sy = static_cast<int>(y) + oy;
                        if (sx < 0 || sy < 0 || sx >= static_cast<int>(w) || sy >= static_cast<int>(h)) {
                            RGBAf &t = scratch.at(x, y);
                            if (fill == 0) {
                                t = RGBAf{0, 0, 0, 0};
                            } else if (fill == 3) {
                                const RGBAf &o = img.at(x, y);
                                t.r = static_cast<float>(1 - o.r);
                                t.g = static_cast<float>(1 - o.g);
                                t.b = static_cast<float>(1 - o.b);
                                t.a = o.a;
                            } else if (fill == 4) {
                                t = img.at(x, y);
                            } else {
                                t.r = t.g = t.b = fill == 1 ? 0.0f : 1.0f;
                                t.a = img.at(x, y).a;
                            }
                        } else {
                            scratch.at(x, y) = img.at(static_cast<std::uint32_t>(sx), static_cast<std::uint32_t>(sy));
                        }
                    }
                }
            }
        }
        std::memcpy(img.data(), scratch.data(), sizeof(RGBAf) * img.pixel_count());
        return true;
    }
    if (id == "extrude") {
        const bool pyramid = std::round(p0) > 0.5;
        const int size = std::max(2, static_cast<int>(std::round(p1)));
        const double depth = std::max(1.0, p2v(par));
        const bool random = std::round(p3v(par)) > 0.5;
        const bool solid = std::round(p4v(par)) > 0.5;
        const std::uint32_t cw2 = std::max<std::uint32_t>(1, static_cast<std::uint32_t>(size));
        const std::uint32_t ch2 = std::max<std::uint32_t>(1, static_cast<std::uint32_t>(size));
        std::uint64_t st = 5150;
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                const std::uint32_t bx = x / cw2;
                const std::uint32_t by = y / ch2;
                const std::uint32_t tx = std::min(bx * cw2 + cw2 / 2, w - 1);
                const std::uint32_t ty = std::min(by * ch2 + ch2 / 2, h - 1);
                RGBAf v = img.at(tx, ty);
                const double lx = static_cast<double>(x % cw2) / cw2 - 0.5;
                const double ly = static_cast<double>(y % ch2) / ch2 - 0.5;
                double shade = 0;
                if (pyramid) {
                    shade = (std::fabs(lx) + std::fabs(ly)) * (depth / 40.0);
                } else {
                    shade = (lx >= 0 ? 0.1 : -0.1) * (depth / 20.0);
                }
                if (random) {
                    const double u = (hash2i(static_cast<int>(bx), static_cast<int>(by), st) % 1000) / 1000.0 - 0.5;
                    shade += u * depth / 60.0;
                }
                if (solid && std::fabs(lx) < 0.28 && std::fabs(ly) < 0.28) shade = 0;
                v.r = static_cast<float>(c01(v.r + shade));
                v.g = static_cast<float>(c01(v.g + shade));
                v.b = static_cast<float>(c01(v.b + shade));
                scratch.at(x, y) = v;
            }
        }
        std::memcpy(img.data(), scratch.data(), sizeof(RGBAf) * img.pixel_count());
        return true;
    }
    return false;
}

}  // namespace pittore::filter
