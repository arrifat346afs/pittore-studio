#pragma once
// Neural filters. Split from engine/filter/filters.h.
#include <string>
#include <vector>

#include "engine/core/image.h"
#include "engine/filter/core/filter_detail.h"
#include "engine/filter/core/filter_params.h"

namespace pittore::filter {

// Umbrella dispatcher (engine/filter/filters.h); defined after this header.
inline void applyFilter(Image& img, const std::string& id,
                        const std::vector<double>& par);

inline bool applyNeural(Image& img, Image& /*scratch*/, const std::string& id,
                       const std::vector<double>& par) {
    using namespace detail;
    const std::uint32_t w = img.width();
    const std::uint32_t h = img.height();
    auto p0 = pv(par, 0, 0.0);
    auto p1 = pv(par, 1, 0.0);
    if (id == "neural.colorize") {
        const double wrm = p0 / 100.0;
        const double strength = std::clamp(p1, 0.0, 100.0) / 100.0;
        for (std::size_t i = 0; i < img.pixel_count(); ++i) {
            const double l = lumaF(img.data()[i]);
            const double cr = c01(l * (1 + wrm * 0.25) + 0.02 * wrm);
            const double cg = c01(l);
            const double cb = c01(l * (1 - wrm * 0.2));
            img.data()[i].r = static_cast<float>(img.data()[i].r * (1 - strength) + cr * strength);
            img.data()[i].g = static_cast<float>(img.data()[i].g * (1 - strength) + cg * strength);
            img.data()[i].b = static_cast<float>(img.data()[i].b * (1 - strength) + cb * strength);
        }
        return true;
    }
    if (id == "neural.color_transfer" || id == "neural.harmonization") {
        const double hue = id == std::string("neural.color_transfer") ? p0 : 0.0;
        const double s = (id == std::string("neural.color_transfer") ? p1 : p0) / 100.0;
        const double extra = id == std::string("neural.color_transfer") ? p2v(par) / 100.0 : p1 / 100.0;
        double ar = 0, ag = 0, ab = 0;
        const std::size_t n = img.pixel_count();
        for (std::size_t i = 0; i < n; ++i) {
            ar += img.data()[i].r;
            ag += img.data()[i].g;
            ab += img.data()[i].b;
        }
        ar /= n;
        ag /= n;
        ab /= n;
        const double hr = hue * 3.141592653589793 / 180.0;
        for (std::size_t i = 0; i < n; ++i) {
            float h, ss, l;
            toHsl(img.data()[i].r, img.data()[i].g, img.data()[i].b, h, ss, l);
            h += static_cast<float>(hr / 6.283185307179586);
            if (h > 1) h -= 1;
            if (h < 0) h += 1;
            float r, g, b;
            fromHsl(h, ss, l, r, g, b);
            img.data()[i].r = static_cast<float>(c01(r * (1 - s * 0.5) + ar * s * 0.5 + extra * 0.05));
            img.data()[i].g = static_cast<float>(c01(g * (1 - s * 0.5) + ag * s * 0.5));
            img.data()[i].b = static_cast<float>(c01(b * (1 - s * 0.5) + ab * s * 0.5));
        }
        return true;
    }
    if (id == "neural.depth_blur") {
        const double f = p0 / 100.0;
        const double r = std::clamp(p1 * 0.5, 0.0, 25.0);
        Image blur(w, h);
        boxBlurInto(img, blur, std::max(1, static_cast<int>(std::round(r))));
        for (std::uint32_t y = 0; y < h; ++y) {
            const double m = c01(std::fabs(static_cast<double>(y) / h - f) * 3.0);
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
    if (id == "neural.face_to_caricature") {
        const double amt = -1.0 * (p0 * 0.3 + p1 * 0.2 + p2v(par) * 0.5) * 0.5;
        applyFilter(img, "spherize", {amt});
        return true;
    }
    if (id == "neural.jpeg_artifacts") {
        const double strength = std::clamp(p0, 0.0, 100.0) / 100.0;
        if (strength > 0.01) {
            medianR(img, 1);
            Image soft = img.clone();
            gaussBlur(soft, 0.8);
            for (std::size_t i = 0; i < img.pixel_count(); ++i) {
                for (int c = 0; c < 3; ++c)
                    img.data()[i][c] = static_cast<float>(img.data()[i][c] * (1 - strength * 0.6) + soft.data()[i][c] * strength * 0.6);
            }
            unsharpF(img, 0.4 * strength + 0.1, 1, 0.02);
        }
        return true;
    }
    if (id == "neural.photo_restoration") {
        const double enhance = std::clamp(p0, 0.0, 100.0) / 100.0;
        const double scratches = std::clamp(p1, 0.0, 100.0) / 100.0;
        const double tone = p2v(par) / 100.0;
        if (scratches > 0.01) {
            Image med = img.clone();
            medianR(med, 2);
            for (std::size_t i = 0; i < img.pixel_count(); ++i) {
                const double d = std::fabs(img.data()[i].r - med.data()[i].r) +
                    std::fabs(img.data()[i].g - med.data()[i].g) +
                    std::fabs(img.data()[i].b - med.data()[i].b);
                if (d > 0.15) {
                    for (int c = 0; c < 3; ++c)
                        img.data()[i][c] = static_cast<float>(img.data()[i][c] * (1 - scratches * 0.8) + med.data()[i][c] * scratches * 0.8);
                }
            }
        }
        medianR(img, 1);
        unsharpF(img, 0.4 * (enhance + 0.5), 1, 0.02);
        if (std::fabs(tone) > 0.01) {
            for (std::size_t i = 0; i < img.pixel_count(); ++i) {
                for (int c = 0; c < 3; ++c)
                    img.data()[i][c] = static_cast<float>(c01((img.data()[i][c] - 0.5) * (1 + tone * 0.3) + 0.5));
            }
        }
        return true;
    }
    if (id == "neural.landscape_mixer") {
        Image orig = img.clone();
        applyFilter(img, "clouds", {3.0});
        const double m = std::clamp(p0, 0.0, 100.0) / 100.0;
        const int bands = std::clamp(static_cast<int>(std::round(p1)), 1, 8);
        Image detail(w, h);
        boxBlurInto(orig, detail, bands);
        for (std::size_t i = 0; i < img.pixel_count(); ++i) {
            for (int c = 0; c < 3; ++c)
                img.data()[i][c] = static_cast<float>(c01((orig.data()[i][c] * (1 - m * 0.5) + img.data()[i][c] * m * 0.5) * (1 - m * 0.2) + detail.data()[i][c] * m * 0.2));
        }
        return true;
    }
    if (id == "neural.makeup_transfer") {
        const double lips = std::clamp(p0, 0.0, 100.0) / 100.0;
        const double eyes = std::clamp(p1, 0.0, 100.0) / 100.0;
        const double skin = std::clamp(p2v(par), 0.0, 100.0) / 100.0;
        Image blur(w, h);
        boxBlurInto(img, blur, 3);
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                RGBAf &t = img.at(x, y);
                const RGBAf &b = blur.at(x, y);
                const double e = std::fabs(t.r - b.r) + std::fabs(t.g - b.g) + std::fabs(t.b - b.b);
                const double m = c01(1.0 - e * 4.0) * skin;
                for (int c = 0; c < 3; ++c)
                    t[c] = static_cast<float>(t[c] * (1 - m) + b[c] * m);
                const double yn = static_cast<double>(y) / h;
                const double lipMask = c01((yn - 0.62) / 0.1) * c01((0.88 - yn) / 0.1) * lips;
                const double eyeMask = (c01((yn - 0.3) / 0.05) * c01((0.45 - yn) / 0.05)) * eyes;
                t.r = static_cast<float>(c01(t.r + lipMask * 0.12 * t.r + eyeMask * 0.05 * (1 - t.r)));
                t.g = static_cast<float>(c01(t.g - lipMask * 0.05 * t.g));
                t.b = static_cast<float>(c01(t.b - lipMask * 0.05 * t.b + eyeMask * 0.03));
            }
        }
        return true;
    }
    if (id == "neural.skin_smoothing") {
        const double s = std::clamp(p0, 0.0, 100.0) / 100.0;
        const double detail = std::clamp(p1, 0.0, 100.0) / 100.0;
        Image blur(w, h);
        boxBlurInto(img, blur, 2 + static_cast<int>(std::round(s * 4.0)));
        for (std::size_t i = 0; i < img.pixel_count(); ++i) {
            const double e = std::fabs(img.data()[i].r - blur.data()[i].r) +
                std::fabs(img.data()[i].g - blur.data()[i].g) +
                std::fabs(img.data()[i].b - blur.data()[i].b);
            const double m = c01(1.0 - e * (2.0 + detail * 0.06)) * s;
            for (int c = 0; c < 3; ++c)
                img.data()[i][c] = static_cast<float>(img.data()[i][c] * (1 - m) + blur.data()[i][c] * m);
        }
        return true;
    }
    if (id == "neural.photo_to_sketch") {
        const double wt = std::max(1.0, p0);
        const double shading = std::clamp(p1, 0.0, 100.0) / 100.0;
        Image gray = img.clone();
        for (std::size_t i = 0; i < img.pixel_count(); ++i) {
            const float l = lumaF(gray.data()[i]);
            gray.data()[i].r = gray.data()[i].g = gray.data()[i].b = l;
        }
        Image blur(w, h);
        boxBlurInto(gray, blur, std::max(1, static_cast<int>(std::round(wt))));
        for (std::size_t i = 0; i < img.pixel_count(); ++i) {
            const double v = c01(0.5 + (gray.data()[i].r - blur.data()[i].r) * 4.0 + gray.data()[i].r * 0.2 * shading);
            img.data()[i].r = img.data()[i].g = img.data()[i].b = static_cast<float>(v);
        }
        return true;
    }
    if (id == "neural.sketch_to_portrait") {
        gaussBlur(img, 1.0);
        for (std::size_t i = 0; i < img.pixel_count(); ++i) {
            for (int c = 0; c < 3; ++c)
                img.data()[i][c] = static_cast<float>(c01(img.data()[i][c] * 0.7 + 0.18 + p0 / 100.0 * 0.1));
        }
        return true;
    }
    if (id == "neural.smart_portrait") {
        const double happy = p0 / 100.0;
        const double surprise = p1 / 100.0;
        const double age = p2v(par) / 100.0;
        const double gaze = p3v(par) / 100.0;
        const double light = p4v(par) / 100.0;
        for (std::size_t i = 0; i < img.pixel_count(); ++i) {
            img.data()[i].r = static_cast<float>(c01(img.data()[i].r * (1 + happy * 0.12 + light * 0.1 + age * 0.05)));
            img.data()[i].g = static_cast<float>(c01(img.data()[i].g * (1 + happy * 0.05 + surprise * 0.04)));
            img.data()[i].b = static_cast<float>(c01(img.data()[i].b * (1 - age * 0.04 + gaze * 0.03)));
        }
        if (std::fabs(happy) + std::fabs(surprise) > 0.05) unsharpF(img, 0.2, 1, 0.0);
        return true;
    }
    if (id == "neural.style_transfer") {
        const int style = std::clamp(static_cast<int>(std::round(p0)), 0, 2);
        const double strength = std::clamp(p1, 0.0, 100.0) / 100.0;
        if (style == 0) posterF(img, 6);
        else if (style == 1) posterF(img, 4);
        else posterF(img, 8);
        medianR(img, style == 2 ? 3 : 2);
        for (std::size_t i = 0; i < img.pixel_count(); ++i) {
            for (int c = 0; c < 3; ++c) {
                const double v = img.data()[i][c];
                const double base = style == 1 ? (v * 1.2 - 0.1) : (v * 1.5 - 0.25);
                img.data()[i][c] = static_cast<float>(c01(v * (1 - strength) + base * strength));
            }
        }
        return true;
    }
    if (id == "neural.super_zoom") {
        const int f = std::clamp(static_cast<int>(std::round(p0)), 2, 4);
        Image big(w * f, h * f);
        for (std::uint32_t y = 0; y < h * f; ++y) {
            for (std::uint32_t x = 0; x < w * f; ++x)
                big.at(x, y) = bilin(img, x / f, y / f);
        }
        Image back(w, h);
        for (std::uint32_t y = 0; y < h; ++y) {
            for (std::uint32_t x = 0; x < w; ++x)
                back.at(x, y) = bilin(big, x * f, y * f);
        }
        std::memcpy(img.data(), back.data(), sizeof(RGBAf) * img.pixel_count());
        unsharpF(img, 0.5, 1, 0.0);
        return true;
    }
    return false;
}

}  // namespace pittore::filter
