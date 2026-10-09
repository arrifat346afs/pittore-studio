// SVG batch2 units: numbers, dash, angle, box, clip, luma, blend.
#include <string>

#include "engine/vector/svg/angle.h"
#include "engine/vector/svg/attr_write.h"
#include "engine/vector/svg/blend.h"
#include "engine/vector/svg/box.h"
#include "engine/vector/svg/clip_path.h"
#include "engine/vector/svg/dash.h"
#include "engine/vector/svg/filter_region.h"
#include "engine/vector/svg/marker_frame.h"
#include "engine/vector/svg/mask_luma.h"
#include "engine/vector/svg/number_list.h"
#include "engine/vector/svg/pattern_tile.h"
#include "engine/vector/svg/text_measure.h"
#include "test_util.h"

using namespace pittore::svg;

namespace {

void test_nums() {
    const auto v = parseDoubles("1, 2.5 -3");
    CHECK_EQ(v.size(), 3u);
    CHECK_NEAR(v[1], 2.5, 1e-9);
}

void test_dash() {
    CHECK(parseDashArray("").empty());
    CHECK_EQ(parseDashArray("4 2").size(), 2u);
    CHECK_EQ(parseDashArray("4").size(), 2u);
    CHECK_NEAR(parseDashOffset("3"), 3, 1e-9);
}

void test_angle() {
    CHECK_NEAR(parseAngleDeg("180"), 180, 1e-9);
    CHECK_NEAR(parseAngleDeg("200grad"), 180, 1e-9);
    CHECK_NEAR(parseAngleDeg("0.5turn"), 180, 1e-9);
}

void test_box() {
    const Box b = parseBox("0 0 40 20");
    CHECK(b.valid);
    CHECK_NEAR(b.w, 40, 1e-9);
    CHECK(!parseBox("0 0").valid);
}

void test_clip() {
    const std::vector<std::pair<double, double>> sq{{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    CHECK(pointInPoly(sq, 5, 5, false));
    CHECK(!pointInPoly(sq, 50, 50, false));
    CHECK(pointInClip({ClipPoly{sq, false}}, 5, 5));
}

void test_luma() {
    CHECK_NEAR(luminance(1, 1, 1), 1, 1e-4);
    CHECK_EQ((int)maskAlpha(1, 1, 1, 1), 255);
    CHECK_EQ((int)maskAlpha(0, 0, 0, 1), 0);
}

void test_blend() {
    float s[4] = {1, 0, 0, 1}, d[4] = {0, 0, 1, 1}, o[4] = {0, 0, 0, 0};
    blendPx(Blend::Normal, s, d, o);
    CHECK_NEAR(o[0], 1, 1e-9);
    blendPx(Blend::Multiply, s, d, o);
    CHECK_NEAR(o[0], 0, 1e-9);
}

void test_region() {
    const FBox b = resolveFilterRegion(0, 0, 100, 100, -0.1, -0.1, 1.2, 1.2);
    CHECK_NEAR(b.x, -10, 1e-9);
    CHECK_NEAR(b.w, 120, 1e-9);
}

void test_tiles() {
    const auto t = tilesFor(0, 0, 20, 20, 0, 0, 10, 10);
    CHECK(!t.empty());
    CHECK(tilesFor(0, 0, 0, 0, 0, 0, 10, 10).empty());
}

void test_markers() {
    const auto f = framesForPolyline({{0, 0}, {10, 0}, {10, 10}}, false);
    CHECK_EQ(f.size(), 3u);
    CHECK(framesForPolyline({{0, 0}}, false).empty());
}

void test_text() {
    const TextBox b = measureText("hi", 10, 0, 0, "start", "alphabetic");
    CHECK(b.w > 0 && b.h > 0);
    CHECK(!fmtNum(1.5).empty());
}

}  // namespace

int main() {
    test_nums();
    test_dash();
    test_angle();
    test_box();
    test_clip();
    test_luma();
    test_blend();
    test_region();
    test_tiles();
    test_markers();
    test_text();
    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}
