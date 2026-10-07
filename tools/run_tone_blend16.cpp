// run_tone_blend.cpp
//
// Standalone harness for Pittore-photo's real engine function
// pittore::compute::applyToneBlend() (src/engine/compute/tone_blend.{h,cpp}),
// unmodified, run with its default ToneBlendParams (i.e. "current settings":
// strength=1.0, color=1.0, contrast=0.0, lowPass=1.0, contentType=0).
//
// Group   = the layer that keeps its detail   (here: the tree photo)
// Backdrop= the layer that donates its tone   (here: the sunset photo)
// Output  = the tree, re-graded to the sunset's tone, tree detail intact.
//
// I/O is raw interleaved 8-bit RGB (no alpha -- both source photos are fully
// opaque), converted to/from the engine's working space (linear-light float)
// exactly as pixel.h documents. Format decode/encode is left to ImageMagick
// via the wrapper script, same pattern as the earlier lighting_match tool.

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <string>

#include "engine/core/pixel.h"
#include "engine/compute/tone_blend.h"

static double srgbToLinear(double c) {
    c /= 65535.0;
    if (c <= 0.04045) return c / 12.92;
    return std::pow((c + 0.055) / 1.055, 2.4);
}

static double linearToSrgb(double c) {
    double v = (c <= 0.0031308) ? c * 12.92
                                : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
    v *= 65535.0;
    if (v < 0.0) v = 0.0;
    if (v > 65535.0) v = 65535.0;
    return v;
}

static std::vector<uint8_t> readRaw(const std::string &path, size_t n) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) { std::fprintf(stderr, "Cannot open %s\n", path.c_str()); std::exit(1); }
    std::vector<uint8_t> buf(n);
    size_t got = std::fread(buf.data(), 1, n, f);
    std::fclose(f);
    if (got != n) {
        std::fprintf(stderr, "Warning: %s has %zu bytes, expected %zu\n",
                     path.c_str(), got, n);
    }
    return buf;
}

int main(int argc, char **argv) {
    if (argc < 6) {
        std::fprintf(stderr,
            "Usage: %s <group_raw> <backdrop_raw> <w> <h> <output_raw>\n"
            "  group_raw/backdrop_raw must both be w*h*3 raw RGB48 (16-bit BE), same "
            "dimensions (same canvas, like two layers).\n", argv[0]);
        return 1;
    }
    const std::string groupPath = argv[1];
    const std::string backdropPath = argv[2];
    const uint32_t w = (uint32_t) std::atoi(argv[3]);
    const uint32_t h = (uint32_t) std::atoi(argv[4]);
    const std::string outPath = argv[5];

    const size_t n = (size_t) w * h;
    std::vector<uint8_t> groupRaw = readRaw(groupPath, n * 6);
    std::vector<uint8_t> backdropRaw = readRaw(backdropPath, n * 6);

    std::vector<pittore::RGBAf> group(n), backdrop(n), dst(n);
    auto u16 = [&](const std::vector<uint8_t>& b, size_t k) {
        return (double)(b[k] | (b[k+1] << 8));  // magick raw16 is LE
    };
    for (size_t i = 0; i < n; ++i) {
        group[i].r = (float) srgbToLinear(u16(groupRaw, i*6));
        group[i].g = (float) srgbToLinear(u16(groupRaw, i*6+2));
        group[i].b = (float) srgbToLinear(u16(groupRaw, i*6+4));
        group[i].a = 1.0f;
        backdrop[i].r = (float) srgbToLinear(u16(backdropRaw, i*6));
        backdrop[i].g = (float) srgbToLinear(u16(backdropRaw, i*6+2));
        backdrop[i].b = (float) srgbToLinear(u16(backdropRaw, i*6+4));
        backdrop[i].a = 1.0f;
    }

    // Current/default settings, verbatim from ToneBlendParams' member
    // initializers in tone_blend.h -- nothing overridden.
    pittore::compute::ToneBlendParams params;

    std::fprintf(stderr,
        "ToneBlendParams (current defaults): strength=%.2f color=%.2f "
        "contrast=%.2f lowPass=%.2f contentType=%d\n",
        params.strength, params.color, params.contrast, params.lowPass,
        params.contentType);

    bool ok = pittore::compute::applyToneBlend(
        group.data(), backdrop.data(), dst.data(), w, h, params);
    if (!ok) {
        std::fprintf(stderr, "applyToneBlend returned false (no-op)\n");
        return 1;
    }

    std::vector<uint8_t> outRaw(n * 6);
    for (size_t i = 0; i < n; ++i) {
        uint16_t vr = (uint16_t) std::lround(linearToSrgb(dst[i].r));
        outRaw[i*6] = vr & 0xff; outRaw[i*6+1] = vr >> 8;
        { uint16_t v = (uint16_t) std::lround(linearToSrgb(dst[i].g)); outRaw[i*6+2] = v & 0xff; outRaw[i*6+3] = v >> 8; }
        { uint16_t v = (uint16_t) std::lround(linearToSrgb(dst[i].b)); outRaw[i*6+4] = v & 0xff; outRaw[i*6+5] = v >> 8; }
    }
    FILE *f = std::fopen(outPath.c_str(), "wb");
    std::fwrite(outRaw.data(), 1, outRaw.size(), f);
    std::fclose(f);
    std::fprintf(stderr, "Wrote %s (%ux%u)\n", outPath.c_str(), w, h);
    return 0;
}
