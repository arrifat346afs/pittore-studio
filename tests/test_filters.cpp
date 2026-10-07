#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/image.h"
#include "engine/filter/filters.h"
#include "test_util.h"

using pittore::Image;
using pittore::RGBAf;
using pittore::filter::FilterDef;

namespace {

Image makeProbe() {
    Image img(25, 19);
    for (std::uint32_t y = 0; y < 19; ++y) {
        for (std::uint32_t x = 0; x < 25; ++x) {
            img.at(x, y) = RGBAf{static_cast<float>(x) / 24.0f,
                                 static_cast<float>(y) / 18.0f,
                                 static_cast<float>((x + y) % 7) / 6.0f,
                                 static_cast<float>(0.25 + 0.5 * ((x * 7 + y) % 2))};
        }
    }
    return img;
}

bool finiteImage(const Image &img) {
    for (std::size_t i = 0; i < img.pixel_count(); ++i) {
        for (int c = 0; c < 4; ++c) {
            if (!std::isfinite(img.data()[i][c])) return false;
        }
    }
    return true;
}

bool rangeImage(const Image &img) {
    for (std::size_t i = 0; i < img.pixel_count(); ++i) {
        for (int c = 0; c < 4; ++c) {
            if (img.data()[i][c] < -0.002f || img.data()[i][c] > 1.002f) return false;
        }
    }
    return true;
}

bool resamplesAlpha(const std::string &id) {
    static const char *ids[] = {
        "color_halftone", "crystallize", "facet", "mosaic", "pointillize",
        "displace", "pinch", "polar", "ripple", "shear", "spherize", "twirl",
        "wave", "zigzag", "glass", "ocean_ripple", "lens_correction",
        "adaptive_wide_angle", "diffuse", "extrude", "tiles", "mosaic_tiles",
        "patchwork", "stained_glass", "texturizer", "offset",
        "neural.face_to_caricature",
    };
    for (const char *k : ids) {
        if (id == k) return true;
    }
    return false;
}

bool alphaKept(const Image &before, const Image &after) {
    for (std::size_t i = 0; i < before.pixel_count(); ++i) {
        if (before.data()[i].a != after.data()[i].a) return false;
    }
    return true;
}

bool samePixels(const Image &a, const Image &b) {
    for (std::size_t i = 0; i < a.pixel_count(); ++i) {
        for (int c = 0; c < 4; ++c) {
            if (a.data()[i][c] != b.data()[i][c]) return false;
        }
    }
    return true;
}

std::vector<double> edgeParams(const FilterDef &d, bool useMin) {
    std::vector<double> v;
    for (const auto &p : d.params) v.push_back(useMin ? p.min : p.max);
    return v;
}

}

static void testEveryFilterRunsClean() {
    const Image probe = makeProbe();
    for (const auto &d : pittore::filter::allFilterDefs()) {
        const std::string id = d.id;
        for (int pass = 0; pass < 3; ++pass) {
            Image img = probe.clone();
            std::vector<double> p = pass == 0 ? pittore::filter::defaultParams(id)
                : pass == 1 ? edgeParams(d, true)
                            : edgeParams(d, false);
            pittore::filter::applyFilter(img, id, p);
            CHECK(finiteImage(img));
            CHECK(rangeImage(img));
            if (!resamplesAlpha(id)) {
                CHECK(alphaKept(probe, img));
            }
        }
        Image a = probe.clone();
        Image b = probe.clone();
        const std::vector<double> dp = pittore::filter::defaultParams(id);
        pittore::filter::applyFilter(a, id, dp);
        pittore::filter::applyFilter(b, id, dp);
        CHECK(samePixels(a, b));
    }
}

static void testMenuIdsResolve() {
    const char *ids[] = {
        "average", "blur", "blur_more", "box_blur", "gaussian_blur", "lens_blur",
        "motion_blur", "radial_blur", "shape_blur", "smart_blur", "surface_blur",
        "field_blur", "iris_blur", "tilt_shift", "path_blur", "spin_blur",
        "displace", "pinch", "polar", "ripple", "shear", "spherize", "twirl",
        "wave", "zigzag", "add_noise", "dust_scratches", "median", "reduce_noise",
        "color_halftone", "crystallize", "facet", "fragment", "mezzotint", "mosaic",
        "pointillize", "flame", "picture_frame", "tree", "fibers", "lens_flare",
        "lighting_effects", "shake_reduction", "smart_sharpen", "unsharp_mask",
        "sharpen", "sharpen_edges", "sharpen_more", "diffuse", "emboss", "extrude",
        "find_edges", "oil_paint", "tiles", "trace_contour", "wind", "custom",
        "high_pass", "maximum", "minimum", "offset", "hsb_hsl", "deinterlace",
        "ntsc_colors", "bump_map", "normal_map", "adaptive_wide_angle", "camera_raw",
        "lens_correction",
    };
    for (const char *id : ids) {
        CHECK(pittore::filter::findFilter(id) != nullptr);
    }
    const char *more[] = {
        "diffuse_glow", "glowing_edges", "glass", "ocean_ripple",
        "colored_pencil", "cutout", "dry_brush", "film_grain", "fresco",
        "neon_glow", "paint_daubs", "palette_knife", "plastic_wrap",
        "poster_edges", "rough_pastels", "smudge_stick", "sponge",
        "underpainting", "watercolor", "accented_edges", "angled_strokes",
        "crosshatch", "dark_strokes", "ink_outlines", "spatter",
        "sprayed_strokes", "sumi_e", "bas_relief", "chalk_charcoal",
        "charcoal", "chrome", "conte_crayon", "graphic_pen",
        "halftone_pattern", "note_paper", "photocopy", "plaster",
        "reticulation", "stamp", "torn_edges", "water_paper", "craquelure",
        "grain", "mosaic_tiles", "patchwork", "stained_glass", "texturizer",
        "neural.colorize", "neural.color_transfer", "neural.depth_blur",
        "neural.face_to_caricature", "neural.harmonization",
        "neural.jpeg_artifacts", "neural.landscape_mixer",
        "neural.makeup_transfer", "neural.photo_restoration",
        "neural.photo_to_sketch", "neural.sketch_to_portrait",
        "neural.skin_smoothing", "neural.smart_portrait",
        "neural.style_transfer", "neural.super_zoom",
    };
    for (const char *id : more) {
        CHECK(pittore::filter::findFilter(id) != nullptr);
    }
    CHECK(pittore::filter::findFilter("no_such_filter") == nullptr);
    CHECK(pittore::filter::defaultParams("no_such_filter").empty());
}

static void testMetadata() {
    std::vector<std::string> seen;
    for (const auto &d : pittore::filter::allFilterDefs()) {
        const std::string id = d.id;
        for (const auto &s : seen) CHECK(s != id);
        seen.push_back(id);
        CHECK(d.name && d.name[0]);
        CHECK(d.category && d.category[0]);
        for (const auto &p : d.params) {
            CHECK(p.id && p.id[0]);
            CHECK(p.label && p.label[0]);
            CHECK(p.suffix != nullptr);
            if (p.kind == 1) {
                CHECK(!p.choices.empty());
                CHECK(p.def >= 0 && p.def < static_cast<double>(p.choices.size()));
                CHECK(p.min == 0 && p.max == static_cast<double>(p.choices.size()) - 1);
                for (const char *opt : p.choices) CHECK(opt && opt[0]);
            } else if (p.kind == 2) {
                CHECK(p.min == 0 && p.max == 1);
                CHECK(p.def == 0 || p.def == 1);
            } else {
                CHECK(p.kind == 0);
                CHECK(p.min <= p.def && p.def <= p.max);
            }
        }
        const std::vector<double> dp = pittore::filter::defaultParams(id);
        CHECK(dp.size() == d.params.size());
    }
    CHECK(!seen.empty());
}

static void testSemantics() {
    Image probe = makeProbe();
    Image img = probe.clone();
    pittore::filter::applyFilter(img, "average", {});
    const RGBAf first = img.at(0, 0);
    for (std::size_t i = 0; i < img.pixel_count(); ++i) {
        CHECK(img.data()[i].r == first.r);
        CHECK(img.data()[i].g == first.g);
        CHECK(img.data()[i].b == first.b);
    }
    CHECK(alphaKept(probe, img));
    {
        double ar = 0, ag = 0, ab = 0, aw = 0;
        for (std::size_t i = 0; i < probe.pixel_count(); ++i) {
            ar += probe.data()[i].r * probe.data()[i].a;
            ag += probe.data()[i].g * probe.data()[i].a;
            ab += probe.data()[i].b * probe.data()[i].a;
            aw += probe.data()[i].a;
        }
        CHECK(std::fabs(first.r - ar / aw) < 1e-5);
        CHECK(std::fabs(first.g - ag / aw) < 1e-5);
        CHECK(std::fabs(first.b - ab / aw) < 1e-5);
    }
    {
        Image trans(6, 6);
        for (std::size_t i = 0; i < trans.pixel_count(); ++i)
            trans.data()[i] = RGBAf{0.0f, 0.0f, 0.0f, 0.0f};
        trans.at(2, 2) = RGBAf{1.0f, 0.5f, 0.25f, 1.0f};
        pittore::filter::applyFilter(trans, "average", {});
        CHECK(std::fabs(trans.at(0, 0).r - 1.0) < 1e-5);
        CHECK(std::fabs(trans.at(0, 0).g - 0.5) < 1e-5);
        CHECK(std::fabs(trans.at(0, 0).b - 0.25) < 1e-5);
        CHECK(trans.at(0, 0).a == 0.0f);
        CHECK(trans.at(2, 2).a == 1.0f);
    }
    {
        Image half = probe.clone();
        pittore::filter::applyFilter(half, "average", {50.0});
        for (std::size_t i = 0; i < img.pixel_count(); ++i) {
            CHECK(std::fabs(half.data()[i].r - (probe.data()[i].r + first.r) * 0.5) < 1e-5);
            CHECK(half.data()[i].a == probe.data()[i].a);
        }
    }
    {
        Image noop = probe.clone();
        pittore::filter::applyFilter(noop, "average", {0.0});
        CHECK(samePixels(probe, noop));
    }

    img = probe.clone();
    pittore::filter::applyFilter(img, "offset", {0.0, 0.0});
    CHECK(samePixels(probe, img));

    img = probe.clone();
    pittore::filter::applyFilter(img, "pinch", {0.0});
    for (std::size_t i = 0; i < img.pixel_count(); ++i) {
        for (int c = 0; c < 3; ++c)
            CHECK(std::fabs(img.data()[i][c] - probe.data()[i][c]) < 1e-4f);
    }

    Image flat(9, 9);
    for (std::size_t i = 0; i < flat.pixel_count(); ++i) flat.data()[i] = RGBAf{0.4f, 0.4f, 0.4f, 1.0f};
    Image f2 = flat.clone();
    pittore::filter::applyFilter(f2, "motion_blur", {0.0, 15.0});
    CHECK(samePixels(flat, f2));
    Image f3 = flat.clone();
    pittore::filter::applyFilter(f3, "find_edges", {});
    for (std::size_t i = 0; i < f3.pixel_count(); ++i) {
        CHECK(f3.data()[i].r > 0.99f);
        CHECK(f3.data()[i].g > 0.99f);
        CHECK(f3.data()[i].b > 0.99f);
    }

    Image sol(2, 1);
    sol.at(0, 0) = RGBAf{0.25f, 0.25f, 0.25f, 1.0f};
    sol.at(1, 0) = RGBAf{0.75f, 0.75f, 0.75f, 1.0f};
    pittore::filter::applyFilter(sol, "solarize", {});
    CHECK(std::fabs(sol.at(0, 0).r - 0.25f) < 1e-6f);
    CHECK(std::fabs(sol.at(1, 0).r - 0.25f) < 1e-6f);

    Image off(5, 4);
    for (std::uint32_t y = 0; y < 4; ++y)
        for (std::uint32_t x = 0; x < 5; ++x)
            off.at(x, y) = RGBAf{static_cast<float>(x), static_cast<float>(y), 0.0f, 1.0f};
    pittore::filter::applyFilter(off, "offset", {2.0, 1.0, 2.0});
    CHECK(off.at(2, 1).r == 0.0f);
    CHECK(off.at(2, 1).g == 0.0f);
    CHECK(off.at(0, 0).r == 3.0f);

    Image before = probe.clone();
    pittore::filter::applyFilter(probe, "no_such_filter", {});
    CHECK(samePixels(before, probe));

    Image surf(11, 11);
    for (std::uint32_t y = 0; y < 11; ++y)
        for (std::uint32_t x = 0; x < 11; ++x)
            surf.at(x, y) = RGBAf{0.5f, 0.5f, 0.5f, 1.0f};
    surf.at(5, 5) = RGBAf{1.0f, 1.0f, 1.0f, 1.0f};
    Image surfOut = surf.clone();
    pittore::filter::applyFilter(surfOut, "surface_blur", {3.0, 15.0});
    CHECK(std::fabs(surfOut.at(0, 0).r - 0.5f) < 0.05f);
    CHECK(surfOut.at(5, 5).r > 0.6f);
}

static void testStrengthZeroIsNoop() {
    Image probe = makeProbe();
    const char *ids[] = {"average", "blur",       "blur_more", "despeckle",
                         "facet",   "sharpen",    "sharpen_edges", "sharpen_more",
                         "find_edges", "solarize", "ntsc_colors"};
    for (const char *id : ids) {
        Image img = probe.clone();
        pittore::filter::applyFilter(img, id, {0.0});
        CHECK(samePixels(probe, img));
    }
}

static void testEveryFilterTakesEffect() {
    for (const auto &d : pittore::filter::allFilterDefs()) {
        const std::string id = d.id;
        if (id == "offset" || id == "camera_raw") continue;
        Image img(25, 19);
        for (std::uint32_t y = 0; y < 19; ++y) {
            for (std::uint32_t x = 0; x < 25; ++x)
                img.at(x, y) = RGBAf{static_cast<float>(x) / 24.0f,
                                     static_cast<float>(y) / 18.0f,
                                     static_cast<float>((x * 3 + y * 5) % 11) / 10.0f,
                                     1.0f};
        }
        Image before = img.clone();
        pittore::filter::applyFilter(img, id, pittore::filter::defaultParams(id));
        bool same = true;
        for (std::size_t i = 0; i < img.pixel_count(); ++i) {
            for (int c = 0; c < 4; ++c) {
                if (img.data()[i][c] != before.data()[i][c]) {
                    same = false;
                    break;
                }
            }
            if (!same) break;
        }
        CHECK(!same);
    }
}

static void testColoredPencilAlpha() {
    Image img(32, 24);
    for (std::uint32_t y = 0; y < 24; ++y) {
        for (std::uint32_t x = 0; x < 32; ++x) {
            if (x < 16)
                img.at(x, y) = RGBAf{0.2f + 0.6f * static_cast<float>(x) / 15.0f,
                                     0.15f + 0.3f * static_cast<float>(y) / 23.0f,
                                     0.1f, 1.0f};
            else
                img.at(x, y) = RGBAf{0.0f, 0.0f, 0.0f, 0.0f};
        }
    }
    pittore::filter::applyFilter(img, "colored_pencil", pittore::filter::defaultParams("colored_pencil"));
    CHECK(finiteImage(img));
    CHECK(rangeImage(img));
    double rl = 0, bl = 0, wl = 0;
    int no = 0, nt = 0;
    float edgeMin = 1.0f;
    for (std::uint32_t y = 0; y < 24; ++y) {
        for (std::uint32_t x = 0; x < 32; ++x) {
            const RGBAf &p = img.at(x, y);
            if (x < 16) {
                rl += p.r;
                bl += p.b;
                ++no;
                if (x >= 8 && x <= 11) {
                    float m = p.r;
                    if (p.g < m) m = p.g;
                    if (p.b < m) m = p.b;
                    if (m < edgeMin) edgeMin = m;
                }
            } else {
                CHECK(p.a == 0.0f);
                wl += (p.r + p.g + p.b) / 3.0;
                ++nt;
            }
        }
    }
    CHECK(nt > 0 && wl / nt > 0.9);
    CHECK(no > 0 && rl / no > bl / no + 0.1);
    CHECK(rl / no < 0.95);
    CHECK(edgeMin > 0.05f);
}

static void testFilters() {
    testEveryFilterRunsClean();
    testMenuIdsResolve();
    testMetadata();
    testSemantics();
    testStrengthZeroIsNoop();
    testEveryFilterTakesEffect();
    testColoredPencilAlpha();
}

#ifndef PITTORE_TEST_NO_MAIN
TEST_MAIN_CALL(testFilters)
#endif
