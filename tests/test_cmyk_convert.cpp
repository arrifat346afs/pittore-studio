// test_cmyk_convert.cpp — naive CMYK <-> RGB core: known values, the pure
// black (k=1) no-NaN edge, alpha passthrough, and rgb->cmyk->rgb round
// trips; plus the profiled import converter (invalid input falls back to
// naive, a real coated profile converts sanely, fixture-gated).
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "test_util.h"
#include "engine/color/convert.h"
#include "engine/color/icc_convert.h"
#include "engine/color/separate.h"

using pittore::RGBAf;
using pittore::color::CmykF;
using pittore::color::CmykToSrgb;
using pittore::color::SrgbToCmyk;
using pittore::color::cmykToRgb;
using pittore::color::rgbToCmyk;

namespace {

// Coated-profile fixture, fetched separately (same discipline as
// PITTORE_AF_FIXTURE): unset or absent skips the profiled assertions.
// No fixture path is baked into the source.
std::string coatedProfilePath() {
    const char* env = std::getenv("PITTORE_ICC_FIXTURE");
    return (env && *env) ? std::string(env) : std::string();
}

bool hasCoatedProfile() {
    const std::string p = coatedProfilePath();
    return !p.empty() && std::filesystem::exists(p);
}

std::vector<std::uint8_t> readFile(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return {};
    std::fseek(f, 0, SEEK_END);
    const long len = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<std::uint8_t> out(len > 0 ? static_cast<std::size_t>(len) : 0);
    if (len > 0)
        out.resize(std::fread(out.data(), 1, out.size(), f));
    std::fclose(f);
    return out;
}

}  // namespace

int main() {
    // Known values, ink coverage 0..1.
    {
        const CmykF white = rgbToCmyk(RGBAf{1.0f, 1.0f, 1.0f, 1.0f});
        CHECK_NEAR(white.c, 0.0f, 1e-6f);
        CHECK_NEAR(white.m, 0.0f, 1e-6f);
        CHECK_NEAR(white.y, 0.0f, 1e-6f);
        CHECK_NEAR(white.k, 0.0f, 1e-6f);
    }
    {
        const CmykF black = rgbToCmyk(RGBAf{0.0f, 0.0f, 0.0f, 1.0f});
        CHECK_NEAR(black.k, 1.0f, 1e-6f);
        CHECK_NEAR(black.c, 0.0f, 1e-6f);
        CHECK_NEAR(black.m, 0.0f, 1e-6f);
        CHECK_NEAR(black.y, 0.0f, 1e-6f);
    }
    {
        const CmykF red = rgbToCmyk(RGBAf{1.0f, 0.0f, 0.0f, 1.0f});
        CHECK_NEAR(red.c, 0.0f, 1e-6f);
        CHECK_NEAR(red.m, 1.0f, 1e-6f);
        CHECK_NEAR(red.y, 1.0f, 1e-6f);
        CHECK_NEAR(red.k, 0.0f, 1e-6f);
    }
    {
        const RGBAf white = cmykToRgb(CmykF{0.0f, 0.0f, 0.0f, 0.0f}, 1.0f);
        CHECK_NEAR(white.r, 1.0f, 1e-6f);
        CHECK_NEAR(white.g, 1.0f, 1e-6f);
        CHECK_NEAR(white.b, 1.0f, 1e-6f);
        CHECK_NEAR(white.a, 1.0f, 1e-6f);
    }
    {
        // Alpha rides through untouched.
        const RGBAf half = cmykToRgb(CmykF{0.0f, 1.0f, 1.0f, 0.0f}, 0.5f);
        CHECK_NEAR(half.r, 1.0f, 1e-6f);
        CHECK_NEAR(half.g, 0.0f, 1e-6f);
        CHECK_NEAR(half.b, 0.0f, 1e-6f);
        CHECK_NEAR(half.a, 0.5f, 1e-6f);
    }
    // Round trips hold to float noise (black excluded: k=1 folds all hues).
    {
        const RGBAf samples[] = {
            {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f},
            {0.0f, 1.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f},
            {0.2f, 0.5f, 0.8f, 1.0f}, {0.9f, 0.1f, 0.4f, 0.3f},
        };
        for (const RGBAf& s : samples) {
            const RGBAf back = cmykToRgb(rgbToCmyk(s), s.a);
            CHECK_NEAR(back.r, s.r, 1e-5f);
            CHECK_NEAR(back.g, s.g, 1e-5f);
            CHECK_NEAR(back.b, s.b, 1e-5f);
            CHECK_NEAR(back.a, s.a, 1e-6f);
        }
    }

    // Profiled import converter: garbage means invalid + naive fallback.
    {
        CmykToSrgb empty(nullptr, 0);
        CHECK(!empty.valid());
        CmykToSrgb junk(reinterpret_cast<const std::uint8_t*>("nope"), 4);
        CHECK(!junk.valid());
        float rgb[3] = {9.0f, 9.0f, 9.0f};
        empty.convert(0.0f, 0.0f, 0.0f, 0.0f, rgb);  // paper white, naive
        CHECK_NEAR(rgb[0], 1.0f, 1e-6f);
        CHECK_NEAR(rgb[1], 1.0f, 1e-6f);
        CHECK_NEAR(rgb[2], 1.0f, 1e-6f);
        empty.convert(0.0f, 0.0f, 0.0f, 1.0f, rgb);  // full black, naive
        CHECK_NEAR(rgb[0], 0.0f, 1e-6f);
        CHECK_NEAR(rgb[1], 0.0f, 1e-6f);
        CHECK_NEAR(rgb[2], 0.0f, 1e-6f);
        // Out-of-range inks clamp instead of exploding.
        empty.convert(2.0f, -1.0f, 0.5f, 0.0f, rgb);
        CHECK(rgb[0] >= 0.0f && rgb[0] <= 1.0f);
        CHECK(rgb[1] >= 0.0f && rgb[1] <= 1.0f);
        CHECK(rgb[2] >= 0.0f && rgb[2] <= 1.0f);
    }

    // Encode-direction converter: garbage means invalid + naive fallback
    // (same discipline as the import side above).
    {
        SrgbToCmyk empty(nullptr, 0);
        CHECK(!empty.valid());
        SrgbToCmyk junk(reinterpret_cast<const std::uint8_t*>("nope"), 4);
        CHECK(!junk.valid());
        float ink[4] = {-9.0f, -9.0f, -9.0f, -9.0f};
        empty.convert(1.0f, 1.0f, 1.0f, ink);  // white, naive: no ink
        CHECK_NEAR(ink[0], 0.0f, 1e-6f);
        CHECK_NEAR(ink[1], 0.0f, 1e-6f);
        CHECK_NEAR(ink[2], 0.0f, 1e-6f);
        CHECK_NEAR(ink[3], 0.0f, 1e-6f);
        empty.convert(0.0f, 0.0f, 0.0f, ink);  // black, naive: pure K
        CHECK_NEAR(ink[3], 1.0f, 1e-6f);
        CHECK_NEAR(ink[0], 0.0f, 1e-6f);
        empty.convert(1.0f, 0.0f, 0.0f, ink);  // red, naive: M+Y
        CHECK_NEAR(ink[0], 0.0f, 1e-6f);
        CHECK_NEAR(ink[1], 1.0f, 1e-6f);
        CHECK_NEAR(ink[2], 1.0f, 1e-6f);
        CHECK_NEAR(ink[3], 0.0f, 1e-6f);
        // Out-of-range light clamps instead of exploding.
        empty.convert(2.0f, -1.0f, 0.5f, ink);
        for (float v : ink) CHECK(v >= 0.0f && v <= 1.0f);
        // Batch path agrees with the per-pixel path.
        const std::uint16_t rgb16[3] = {40000, 12000, 60000};
        std::uint16_t batch[4] = {1, 2, 3, 4};
        empty.convert16(rgb16, batch, 1);
        float single[4] = {0, 0, 0, 0};
        empty.convert(40000.0f / 65535.0f, 12000.0f / 65535.0f,
                      60000.0f / 65535.0f, single);
        for (int i = 0; i < 4; ++i)
            CHECK_NEAR(static_cast<float>(batch[i]) / 65535.0f, single[i],
                       2e-4f);
    }

    // Separation helper (naive path): planes come out in ink convention,
    // alpha rides through as the fifth sample only when asked.
    {
        const std::uint16_t rgba[8] = {
            65535, 65535, 65535, 30000,  // white, half alpha
            0,     0,     0,     65535,  // black, opaque
        };
        std::vector<std::uint16_t> planes;
        pittore::color::separateRgba16(rgba, 2, nullptr, 0, true, planes);
        CHECK(planes.size() == 10);
        CHECK_NEAR(planes[0], 0, 0);      // white -> no ink
        CHECK_NEAR(planes[3], 0, 0);
        CHECK(planes[4] == 30000);        // alpha untouched
        CHECK(planes[5] == 0);            // black -> no CMY (full GCR)
        CHECK(planes[6] == 0);
        CHECK(planes[7] == 0);
        CHECK(planes[8] == 65535);        // black -> full K
        CHECK(planes[9] == 65535);        // alpha untouched
        pittore::color::separateRgba16(rgba, 2, nullptr, 0, false, planes);
        CHECK(planes.size() == 8);
        pittore::color::separateRgba16(rgba, 2, nullptr, 0, true, planes);
        CHECK(planes.size() == 10);
    }

    // A real coated profile converts sanely (relative + BPC): paper white
    // stays white, full ink stays near-black, pure cyan lands in a sane
    // blue-green. Fixture-gated on PITTORE_ICC_FIXTURE.
    if (!hasCoatedProfile()) {
        std::printf("  (skip: PITTORE_ICC_FIXTURE not set or absent)\n");
    } else {
        const std::vector<std::uint8_t> icc = readFile(coatedProfilePath().c_str());
        CHECK(!icc.empty());
        CmykToSrgb coated(icc.data(), icc.size());
#ifdef PITTORE_LCMS2
        CHECK(coated.valid());
        float rgb[3] = {9.0f, 9.0f, 9.0f};
        coated.convert(0.0f, 0.0f, 0.0f, 0.0f, rgb);
        CHECK_NEAR(rgb[0], 1.0f, 0.05f);
        CHECK_NEAR(rgb[1], 1.0f, 0.05f);
        CHECK_NEAR(rgb[2], 1.0f, 0.05f);
        coated.convert(0.0f, 0.0f, 0.0f, 1.0f, rgb);
        CHECK(rgb[0] < 0.15f && rgb[1] < 0.15f && rgb[2] < 0.15f);
        coated.convert(1.0f, 0.0f, 0.0f, 0.0f, rgb);
        CHECK(rgb[0] < 0.4f && rgb[1] > 0.4f && rgb[2] > 0.4f);
#else
        CHECK(!coated.valid());  // minimal builds stay naive, honestly
        float rgb[3] = {9.0f, 9.0f, 9.0f};
        coated.convert(1.0f, 0.0f, 0.0f, 0.0f, rgb);  // naive: exact cyan
        CHECK_NEAR(rgb[0], 0.0f, 1e-6f);
        CHECK_NEAR(rgb[1], 1.0f, 1e-6f);
        CHECK_NEAR(rgb[2], 1.0f, 1e-6f);
#endif
#ifdef PITTORE_LCMS2
        // Encode direction through the same profile: white separates to
        // (almost) no ink, black to a substantial lay, red to M+Y with no
        // cyan — and an in-gamut colour survives ->CMYK->RGB within a
        // couple of code values.
        SrgbToCmyk sep(icc.data(), icc.size());
        CHECK(sep.valid());
        float ink[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        sep.convert(1.0f, 1.0f, 1.0f, ink);
        CHECK(ink[0] + ink[1] + ink[2] + ink[3] < 0.15f);
        sep.convert(0.0f, 0.0f, 0.0f, ink);
        CHECK(ink[0] + ink[1] + ink[2] + ink[3] > 0.5f);
        sep.convert(1.0f, 0.0f, 0.0f, ink);
        CHECK(ink[0] < 0.2f && ink[1] > 0.4f && ink[2] > 0.4f);
        const RGBAf roundTrip[] = {
            {1.0f, 1.0f, 1.0f, 1.0f}, {0.5f, 0.5f, 0.5f, 1.0f},
            {0.8f, 0.7f, 0.6f, 1.0f}, {0.2f, 0.4f, 0.6f, 1.0f},
        };
        for (const RGBAf& s : roundTrip) {
            float inks[4] = {0, 0, 0, 0};
            sep.convert(s.r, s.g, s.b, inks);
            float back[3] = {0, 0, 0};
            coated.convert(inks[0], inks[1], inks[2], inks[3], back);
            CHECK_NEAR(back[0], s.r, 0.06f);
            CHECK_NEAR(back[1], s.g, 0.06f);
            CHECK_NEAR(back[2], s.b, 0.06f);
        }
        // The separation helper picks up the profile too.
        {
            const std::uint16_t whitePx[4] = {65535, 65535, 65535, 12345};
            std::vector<std::uint16_t> planes;
            pittore::color::separateRgba16(whitePx, 1, icc.data(),
                                            icc.size(), true, planes);
            CHECK(planes.size() == 5);
            CHECK(planes[0] + planes[1] + planes[2] + planes[3] < 9000);
            CHECK(planes[4] == 12345);
        }
#endif
    }

    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return pittore_test::failures() == 0 ? 0 : 1;
}
