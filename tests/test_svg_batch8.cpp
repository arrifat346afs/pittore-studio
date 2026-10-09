// SVG batch8 units: text, color, geom, affine.
#include "engine/vector/svg/affine_apply.h"
#include "engine/vector/svg/affine_build.h"
#include "engine/vector/svg/affine_inv.h"
#include "engine/vector/svg/alpha_mix.h"
#include "engine/vector/svg/arc_flags.h"
#include "engine/vector/svg/bbox_merge.h"
#include "engine/vector/svg/bezier_flat.h"
#include "engine/vector/svg/bidi_flag.h"
#include "engine/vector/svg/bounds.h"
#include "engine/vector/svg/contrast_pick.h"
#include "engine/vector/svg/hsl_build.h"
#include "engine/vector/svg/line_break.h"
#include "engine/vector/svg/luma_gray.h"
#include "engine/vector/svg/named_color.h"
#include "engine/vector/svg/point_xform.h"
#include "engine/vector/svg/poly_close.h"
#include "engine/vector/svg/quad_split.h"
#include "engine/vector/svg/rect_corners.h"
#include "engine/vector/svg/rgb_hex.h"
#include "engine/vector/svg/smooth_hint.h"
#include "engine/vector/svg/text_xform.h"
#include "engine/vector/svg/white_wrap.h"
#include "engine/vector/svg/word_split.h"
#include "test_util.h"

using namespace pittore::svg;

namespace {

void test_text() {
    CHECK_EQ(splitWords("a  b").size(), 2u);
    CHECK_EQ(wrapLines({60, 60}, 100).size(), 1u);
    CHECK(isRtlDir("rtl"));
    CHECK(applyTextTransform("hi", "uppercase") == "HI");
    CHECK(collapseWhite("a   b", false) == "a b");
}

void test_color() {
    float o[3] = {0, 0, 0};
    hslToRgb(0, 1, 0.5f, o);
    CHECK(o[0] > 0.9f);
    CHECK_NEAR(overAlpha(0.5f, 0.5f), 0.75, 1e-6);
    CHECK_NEAR(grayOf(1, 1, 1), 1, 1e-4);
    CHECK(useWhiteText(0, 0, 0));
    CHECK(!rgbHex(1, 0, 0).empty());
    float c[4] = {0, 0, 0, 0};
    CHECK(namedRgba("yellow", c));
    CHECK(namedRgba("transparent", c));
}

void test_geom() {
    double rx = 100, ry = 100;
    clampRadii(10, 10, rx, ry);
    CHECK_NEAR(rx, 5, 1e-9);
    CHECK(parseArcFlags("1", "0").large);
    CHECK(cubicFlatness(0, 0, 1, 1, 2, 2, 3, 3) >= 0);
    double a = 0, b = 0, c2 = 0, d = 0;
    quadToCubic(0, 0, 1, 1, 2, 2, a, b, c2, d);
    reflectPoint(0, 0, 1, 1, a, b);
    std::vector<std::pair<double, double>> r{{0, 0}, {1, 0}};
    closeRing(r);
    CHECK_EQ(r.size(), 2u);
    CHECK_EQ(xformPoints({{1, 0}}, makeTranslate(1, 0)).size(), 1u);
    CHECK_EQ(mergeBoxes({}).empty, true);
    CHECK(makeScale(2, 2).a == 2);
    double ox = 0, oy = 0;
    applyAffine(makeTranslate(1, 2), 0, 0, ox, oy);
    CHECK_NEAR(ox, 1, 1e-9);
    Affine inv;
    CHECK(invertAffine(makeScale(2, 2), inv));
    CHECK(!invertAffine(Affine{0, 0, 0, 0, 0, 0}, inv));
}

}  // namespace

int main() {
    test_text();
    test_color();
    test_geom();
    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}
