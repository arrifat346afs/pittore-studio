#pragma once
// Artistic filters. Split from engine/filter/filters.h.
#include <string>
#include <vector>

#include "engine/core/image.h"
#include "engine/filter/core/filter_detail.h"
#include "engine/filter/core/filter_params.h"

namespace pittore::filter {

inline bool applyArtistic(Image& img, Image& /*scratch*/, const std::string& id,
                       const std::vector<double>& par) {
    using namespace detail;
    const std::uint32_t w = img.width();
    const std::uint32_t h = img.height();
    auto p0 = pv(par, 0, 0.0);
    auto p1 = pv(par, 1, 0.0);
    if (id == std::string("colored_pencil")) {
        const double pressure01 = std::clamp(p1, 0.0, 15.0) / 15.0;
        const double paper01 = std::clamp(p2v(par), 0.0, 50.0) / 50.0;
        const std::size_t n = static_cast<std::size_t>(w) * h;
        if (n == 0) return true;
        const int wi = static_cast<int>(w);
        const int hi = static_cast<int>(h);
        std::vector<float> gray(n);
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                const RGBAf &t = img.at(x, y);
                const float lum = 0.299f * t.r + 0.587f * t.g + 0.114f * t.b;
                gray[static_cast<std::size_t>(y) * w + x] = t.a > 0.001f ? lum : 1.0f;
            }
        }
        std::vector<float> grad(n);
        std::vector<unsigned char> dir(n, 0);
        float gmax = 1e-6f;
        for (int y = 0; y < hi; ++y) {
            const int yp = y + 1 >= hi ? hi - 1 : y + 1;
            for (int x = 0; x < wi; ++x) {
                const int xp = x + 1 >= wi ? wi - 1 : x + 1;
                const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(wi) + static_cast<std::size_t>(x);
                const float gx = gray[i] - gray[static_cast<std::size_t>(y) * static_cast<std::size_t>(wi) + static_cast<std::size_t>(xp)];
                float ax = gx;
                if (ax < 0) ax = -ax;
                const float gy = gray[i] - gray[static_cast<std::size_t>(yp) * static_cast<std::size_t>(wi) + static_cast<std::size_t>(x)];
                float ay = gy;
                if (ay < 0) ay = -ay;
                const float m = std::sqrt(ax * ax + ay * ay);
                grad[i] = m;
                if (m > gmax) gmax = m;
                const float t = ay / (ax + 1e-6f);
                int zb = 2;
                if (t < 0.1989f) zb = 0;
                else if (t < 0.6682f) zb = 1;
                else if (t < 1.4966f) zb = 2;
                else if (t < 5.0273f) zb = 3;
                else zb = 4;
                int ab = zb;
                if (gx >= 0) {
                    if (gy < 0) ab = (8 - zb) & 7;
                } else {
                    if (gy >= 0) ab = (8 - zb) & 7;
                }
                dir[i] = static_cast<unsigned char>((ab + 4) & 7);
            }
        }
        const float ggain = static_cast<float>(0.5 + pressure01 * 1.6);
        for (std::size_t i = 0; i < n; ++i) grad[i] = grad[i] / gmax * ggain;
        Image glum(w, h);
        for (std::size_t i = 0; i < n; ++i) {
            glum.data()[i].r = glum.data()[i].g = glum.data()[i].b = gray[i];
            glum.data()[i].a = 1.0f;
        }
        Image gtmp(w, h);
        const int lumR = std::clamp(1 + static_cast<int>(p0 / 8.0), 1, 4);
        boxBlurInto(glum, gtmp, lumR);
        std::vector<float> dodge(n);
        for (std::size_t i = 0; i < n; ++i) {
            const double denom = 1.0 - static_cast<double>(gtmp.data()[i].r);
            double v = static_cast<double>(gray[i]) / (denom > 0.04 ? denom : 0.04);
            if (v > 1.0) v = 1.0;
            dodge[i] = static_cast<float>(v * v * (3.0 - 2.0 * v));
        }
        static const int SDX[8] = {1, 2, 1, 1, 0, -1, -1, -2};
        static const int SDY[8] = {0, 1, 1, 2, 1, 2, 1, 1};
        const int tapR = std::clamp(static_cast<int>(std::round(pv(par, 3, 2.0))), 1, 3);
        std::vector<float> stroke(n, 0.0f);
        float smax = 1e-6f;
        for (int y = 0; y < hi; ++y) {
            for (int x = 0; x < wi; ++x) {
                const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(wi) + static_cast<std::size_t>(x);
                const int d = dir[i];
                const int e = (d + 2) & 7;
                double s1 = 0.0;
                double c1 = 0.0;
                double s2 = 0.0;
                double c2 = 0.0;
                for (int k = -tapR; k <= tapR; ++k) {
                    int qx = x + SDX[d] * k;
                    int qy = y + SDY[d] * k;
                    if (qx < 0) qx = 0;
                    if (qx >= wi) qx = wi - 1;
                    if (qy < 0) qy = 0;
                    if (qy >= hi) qy = hi - 1;
                    const std::size_t q = static_cast<std::size_t>(qy) * static_cast<std::size_t>(wi) + static_cast<std::size_t>(qx);
                    if (dir[q] == d) {
                        s1 += grad[q];
                        c1 += 1.0;
                    }
                    int ex = x + SDX[e] * k;
                    int ey = y + SDY[e] * k;
                    if (ex < 0) ex = 0;
                    if (ex >= wi) ex = wi - 1;
                    if (ey < 0) ey = 0;
                    if (ey >= hi) ey = hi - 1;
                    const std::size_t r = static_cast<std::size_t>(ey) * static_cast<std::size_t>(wi) + static_cast<std::size_t>(ex);
                    if (dir[r] == d || dir[r] == e) {
                        s2 += grad[r];
                        c2 += 1.0;
                    }
                }
                double comb = 0.0;
                if (c1 > 0.5) comb += s1 / c1;
                if (c2 > 0.5) comb += 0.6 * s2 / c2;
                stroke[i] = static_cast<float>(comb);
                if (stroke[i] > smax) smax = stroke[i];
            }
        }
        const double strokeDark = 0.45 + pressure01 * 0.45;
        const double chromaScale = std::clamp(pv(par, 5, 100.0), 0.0, 200.0) / 100.0;
        const double grainAmt = std::clamp(pv(par, 4, 15.0), 0.0, 50.0) / 50.0 * 0.12;
        for (int y = 0; y < hi; ++y) {
            for (int x = 0; x < wi; ++x) {
                const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(wi) + static_cast<std::size_t>(x);
                RGBAf &t = img.at(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
                const float a = t.a;
                if (a <= 0.001f) {
                    t.r = 0.98f;
                    t.g = 0.98f;
                    t.b = 0.98f;
                    continue;
                }
                const double snr = c01(static_cast<double>(stroke[i]) / smax);
                const double sn = snr <= 0.0 ? 0.0 : snr >= 1.0 ? 1.0 : std::sqrt(snr);
                const double line = c01((1.0 - sn * strokeDark) * (0.25 + 0.75 * static_cast<double>(dodge[i])));
                const double sm = line;
                const double k = paper01 * sm * sm * (3.0 - 2.0 * sm);
                const double shaded = static_cast<double>(gray[i]) * line;
                const double hatch = c01((sn - 0.35) / 0.65) * (0.30 + 0.45 * pressure01);
                const double yp = c01((shaded * (1.0 - k) + k) * (1.0 - hatch * 0.25) + hatch * 0.30);
                const double r0 = t.r;
                const double g0 = t.g;
                const double b0 = t.b;
                const double yc = 0.299 * r0 + 0.587 * g0 + 0.114 * b0;
                double u = (b0 - yc) * 0.5 + 0.5;
                double vch = (r0 - yc) * 0.5 + 0.5;
                u = 0.5 + (u - 0.5) * chromaScale;
                vch = 0.5 + (vch - 0.5) * chromaScale;
                double rr = yp + 2.0 * (vch - 0.5);
                double bb = yp + 2.0 * (u - 0.5);
                double gg = (yp - 0.299 * rr - 0.114 * bb) / 0.587;
                const double nz = (hash2i(x, y, 4242) % 1000) / 1000.0 - 0.5;
                const double gm = 1.0 + nz * grainAmt * (0.35 + 0.65 * (1.0 - yp)) * 2.0;
                t.r = static_cast<float>(c01(rr * gm));
                t.g = static_cast<float>(c01(gg * gm));
                t.b = static_cast<float>(c01(bb * gm));
                t.a = a;
            }
        }
        return true;
    }
    if (id == "oil_paint" || id == "paint_daubs" || id == "palette_knife" || id == "smudge_stick" ||
        id == "fresco" || id == "dry_brush" || id == "watercolor" || id == "rough_pastels" ||
        id == "angled_strokes" || id == "crosshatch" || id == "dark_strokes" || id == "sumi_e" ||
        id == "sponge" || id == "underpainting" || id == "cutout" || id == "poster_edges" ||
        id == "stamp" || id == "photocopy" || id == "charcoal" || id == "chalk_charcoal" ||
        id == "graphic_pen" || id == "note_paper" || id == "plaster" || id == "reticulation" ||
        id == "torn_edges" || id == "bas_relief" || id == "chrome" || id == "conte_crayon" ||
        id == "spatter" || id == "sprayed_strokes" ||
        id == "ink_outlines") {
        const int p2round = static_cast<int>(std::round(p2v(par)));
        const int levels = (id == std::string("cutout")) ? std::max(2, static_cast<int>(std::round(p0)))
            : (id == std::string("poster_edges")) ? (p2round < 2 ? 0 : std::min(8, p2round))
            : (id == std::string("conte_crayon")) ? std::max(1, static_cast<int>(std::round(p0))) : 0;
        if (id == std::string("cutout") && p1 > 0.5) gaussBlur(img, std::min(4.0, p1 * 0.4));
        if (id == std::string("palette_knife") && p2v(par) > 0.5) gaussBlur(img, std::min(3.0, p2v(par)));
        if (id == std::string("sponge") && p2v(par) > 1.0) gaussBlur(img, std::min(3.0, p2v(par) * 0.3));
        if (id == std::string("stamp") && p1 > 1.0) gaussBlur(img, std::min(3.0, p1 * 0.2));
        if (id == std::string("chrome") && p1 > 0.5) gaussBlur(img, std::min(3.0, p1 * 0.4));
        int r = 1 + static_cast<int>(std::round(std::fabs(p0) / 8.0));
        if (id == std::string("oil_paint")) r = std::clamp(static_cast<int>(std::round(p0 / 2.0)), 1, 6);
        if (id == std::string("paint_daubs")) r = std::clamp(static_cast<int>(std::round(p0 / 4.0)), 1, 6);
        if (id == std::string("palette_knife")) r = std::clamp(static_cast<int>(std::round(p0 / 8.0)), 1, 6);
        if (levels > 0) posterF(img, levels);
        medianR(img, std::min(6, r));
        if (id == std::string("poster_edges") || id == std::string("photocopy") ||
            id == std::string("stamp") || id == std::string("charcoal") ||
            id == std::string("chalk_charcoal") || id == std::string("graphic_pen")) {
            const double dark = id == std::string("photocopy") ? p1 / 50.0 * 0.3 : 0.0;
            const double gain = id == std::string("poster_edges") ? std::max(0.5, p0) : 4.0;
            const double strength = id == std::string("poster_edges") ? std::max(0.0, p1) / 4.0 + 0.25 : 1.0;
            const double bal = id == std::string("stamp") ? std::clamp(p0, 1.0, 50.0) / 50.0
                : id == std::string("graphic_pen") ? std::clamp(p0, 0.0, 100.0) / 100.0 : -1.0;
            for (std::uint32_t y = 0; y < h; ++y) {
                for (std::uint32_t x = 0; x < w; ++x) {
                    const double e = edgeMag(img, x, y);
                    const double m = c01(e * gain);
                    RGBAf &t = img.at(x, y);
                    if (bal >= 0.0) {
                        const double l = lumaF(t);
                        const double rough = (hash2i(static_cast<int>(x), static_cast<int>(y), 99) % 1000) / 1000.0 - 0.5;
                        const double v = (l + rough * m * 0.4 < bal) ? 0.0 : 1.0;
                        t.r = t.g = t.b = static_cast<float>(v);
                    } else {
                        t.r = static_cast<float>(c01(t.r * (1 - m * strength) - dark * m));
                        t.g = static_cast<float>(c01(t.g * (1 - m * strength) - dark * m));
                        t.b = static_cast<float>(c01(t.b * (1 - m * strength) - dark * m));
                    }
                }
            }
        }
        if (id == std::string("chrome") || id == std::string("bas_relief") ||
            id == std::string("plaster") || id == std::string("note_paper")) {
            const double smooth = id == std::string("bas_relief") ? p1
                : id == std::string("plaster") ? p1
                : id == std::string("note_paper") ? p1 * 0.2
                : id == std::string("chrome") ? p1 : 2.0;
            const int lightIdx = id == std::string("bas_relief") ? std::clamp(static_cast<int>(std::round(p2v(par))), 0, 3)
                : id == std::string("plaster") ? std::clamp(static_cast<int>(std::round(p3v(par))), 0, 3) : 0;
            const double la[4] = {0.0, 3.141592653589793, 1.5707963267948966, -1.5707963267948966};
            const double ldx = std::cos(la[lightIdx]);
            const double ldy = std::sin(la[lightIdx]);
            const double detail = id == std::string("bas_relief") ? std::max(1.0, p0) / 8.0 : 1.0;
            Image gray = img.clone();
            for (std::size_t i = 0; i < img.pixel_count(); ++i) {
                const float l = lumaF(gray.data()[i]);
                gray.data()[i].r = gray.data()[i].g = gray.data()[i].b = l;
            }
            gaussBlur(gray, 0.5 + smooth * 0.3);
            const double grainAmt = id == std::string("note_paper") ? p1 / 50.0 * 0.15 : 0.0;
            const double balance = id == std::string("note_paper") ? std::clamp(p0, 0.0, 50.0) / 50.0 : 1.0;
            for (std::uint32_t y = 0; y < h; ++y) {
                for (std::uint32_t x = 0; x < w; ++x) {
                    const double gx = lumaF(bilin(gray, static_cast<double>(x) + ldx * detail, static_cast<double>(y) + ldy * detail));
                    const double gy = lumaF(bilin(gray, static_cast<double>(x) - ldx * detail, static_cast<double>(y) - ldy * detail));
                    double v = c01(0.5 + (gx - gy) * 1.5);
                    if (grainAmt > 0.001) {
                        const double n = fbm(static_cast<double>(x) * 0.08, static_cast<double>(y) * 0.08, 3, 41);
                        v = c01(v * (1 - grainAmt) + n * grainAmt + (balance - 0.5) * 0.3);
                    }
                    if (id == std::string("plaster")) {
                        const double rel = std::clamp(p2v(par), 0.0, 50.0) / 50.0;
                        v = c01(0.5 + (v - 0.5) * (0.4 + rel * 1.6));
                    }
                    img.at(x, y).r = img.at(x, y).g = img.at(x, y).b = static_cast<float>(v);
                }
            }
        }
        if (id == std::string("watercolor") || id == std::string("fresco") ||
            id == std::string("rough_pastels") || id == std::string("sponge") ||
            id == std::string("underpainting") || id == std::string("torn_edges") ||
            id == std::string("reticulation") || id == std::string("dry_brush")) {
            const double relief = id == std::string("rough_pastels") ? p4v(par)
                : id == std::string("underpainting") ? p4v(par)
                : id == std::string("torn_edges") ? p1 : 8.0;
            const std::uint64_t seed = id == std::string("rough_pastels") || id == std::string("underpainting")
                ? static_cast<std::uint64_t>(std::round(p2v(par)) * 131 + 9) : 9;
            const double gstrength = 0.10 + std::clamp(relief, 0.0, 50.0) / 50.0 * 0.18;
            for (std::uint32_t y = 0; y < h; ++y) {
                for (std::uint32_t x = 0; x < w; ++x) {
                    const double n = fbm(static_cast<double>(x) * 0.05, static_cast<double>(y) * 0.05, 3, seed);
                    RGBAf &t = img.at(x, y);
                    const double m = 1.0 - gstrength + n * gstrength * 2.0;
                    t.r = static_cast<float>(c01(t.r * m));
                    t.g = static_cast<float>(c01(t.g * m));
                    t.b = static_cast<float>(c01(t.b * m));
                }
            }
            if (id == std::string("torn_edges")) {
                const double co = std::clamp(p2v(par), 0.0, 100.0) / 100.0;
                for (std::size_t i = 0; i < img.pixel_count(); ++i) {
                    for (int c = 0; c < 3; ++c)
                        img.data()[i][c] = static_cast<float>(c01((img.data()[i][c] - 0.5) * (1 + co) + 0.5));
                }
            }
            if (id == std::string("reticulation")) {
                const double dens = std::clamp(p0, 0.0, 50.0) / 50.0;
                const double fg = std::clamp(p1, 0.0, 50.0) / 50.0;
                const double bg = std::clamp(p2v(par), 0.0, 50.0) / 50.0;
                for (std::size_t i = 0; i < img.pixel_count(); ++i) {
                    const double l = lumaF(img.data()[i]);
                    const double v = l > dens ? fg : bg;
                    img.data()[i].r = img.data()[i].g = img.data()[i].b = static_cast<float>(v);
                }
            }
        }
        if (id == std::string("spatter") ||
            id == std::string("sprayed_strokes") || id == std::string("ink_outlines")) {
            for (std::size_t i = 0; i < img.pixel_count(); ++i) {
                const double g = (hash2i(0, static_cast<int>(i & 0xffffff), 4242 + i) % 1000) / 1000.0;
                const double m = 0.9 + g * 0.2;
                for (int c = 0; c < 3; ++c)
                    img.data()[i][c] = static_cast<float>(c01(img.data()[i][c] * m + (g - 0.5) * 0.05));
            }
        }
    if (id == std::string("ink_outlines")) {
            const double dk = 1.0 + std::clamp(p1, 0.0, 50.0) / 50.0 * 0.6;
            const double lt = 1.0 + std::clamp(p2v(par), 0.0, 50.0) / 50.0 * 0.4;
            for (std::size_t i = 0; i < img.pixel_count(); ++i) {
                const double l = lumaF(img.data()[i]);
                const double k = l < 0.5 ? dk : lt;
                for (int c = 0; c < 3; ++c)
                    img.data()[i][c] = static_cast<float>(c01((img.data()[i][c] - 0.5) * k + 0.5));
            }
        }
        if (id == std::string("sumi_e")) {
            const double co = 1.0 + std::clamp(p2v(par), 0.0, 40.0) / 40.0;
            for (std::size_t i = 0; i < img.pixel_count(); ++i) {
                for (int c = 0; c < 3; ++c)
                    img.data()[i][c] = static_cast<float>(c01((img.data()[i][c] - 0.5) * co + 0.5));
            }
        }
        if (id == std::string("dark_strokes")) {
            const double bk = std::clamp(p1, 0.0, 10.0) / 10.0;
            const double wt = std::clamp(p2v(par), 0.0, 10.0) / 10.0;
            for (std::size_t i = 0; i < img.pixel_count(); ++i) {
                for (int c = 0; c < 3; ++c) {
                    double v = img.data()[i][c];
                    v = v < 0.5 ? v * (1 - bk * 0.5) : v + (1 - v) * wt * 0.3;
                    img.data()[i][c] = static_cast<float>(c01(v));
                }
            }
        }
        if (id == std::string("chalk_charcoal")) {
            const double pressure = 1.0 + std::clamp(p2v(par), 0.0, 5.0) / 5.0 * 0.5;
            for (std::size_t i = 0; i < img.pixel_count(); ++i) {
                for (int c = 0; c < 3; ++c)
                    img.data()[i][c] = static_cast<float>(c01((img.data()[i][c] - 0.5) * pressure + 0.5));
            }
        }
        if (id == std::string("conte_crayon") && std::round(p6v(par)) > 0.5) {
            for (std::size_t i = 0; i < img.pixel_count(); ++i) {
                for (int c = 0; c < 3; ++c)
                    img.data()[i][c] = static_cast<float>(1.0 - img.data()[i][c]);
            }
        }
        unsharpF(img, 0.3, 1, 0.0);
        return true;
    }
        if (id == std::string("neon_glow")) {
            const double size = std::max(0.0, p0);
            const double bright = std::clamp(p1, 0.0, 50.0) / 15.0;
            Image soft = img.clone();
            if (size > 0.5) gaussBlur(soft, size * 0.5);
            for (std::uint32_t y = 0; y < h; ++y) {
                for (std::uint32_t x = 0; x < w; ++x) {
                    const double e = edgeMag(soft, x, y);
                    RGBAf &t = img.at(x, y);
                    t.r = static_cast<float>(c01(t.r + e * bright));
                    t.g = static_cast<float>(c01(t.g + e * bright * 0.8));
                    t.b = static_cast<float>(c01(t.b + e * bright * 0.6));
                }
            }
            return true;
        }
    if (id == std::string("plastic_wrap")) {
            const double strength = std::clamp(p0, 0.0, 20.0) / 10.0;
            const double smooth = std::clamp(p2v(par), 1.0, 15.0);
            Image soft = img.clone();
            gaussBlur(soft, smooth * 0.4);
            for (std::uint32_t y = 0; y < h; ++y) {
                for (std::uint32_t x = 0; x < w; ++x) {
                    const RGBAf &o = img.at(x, y);
                    const RGBAf &b = soft.at(x, y);
                    const double e = edgeMag(soft, x, y);
                    RGBAf &t = img.at(x, y);
                    t.r = static_cast<float>(c01(b.r + e * strength * 0.8 + (o.r - b.r) * 0.3));
                    t.g = static_cast<float>(c01(b.g + e * strength * 0.7 + (o.g - b.g) * 0.3));
                    t.b = static_cast<float>(c01(b.b + e * strength * 0.6 + (o.b - b.b) * 0.3));
                }
            }
            return true;
    }
    return false;
}

}  // namespace pittore::filter
