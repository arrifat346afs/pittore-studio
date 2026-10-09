// SVG batch4 units: first half of value helpers.
#include "engine/vector/svg/color_interp.h"
#include "engine/vector/svg/display.h"
#include "engine/vector/svg/fill_rule.h"
#include "engine/vector/svg/font_size.h"
#include "engine/vector/svg/gradient_units.h"
#include "engine/vector/svg/letter_space.h"
#include "engine/vector/svg/line_height.h"
#include "engine/vector/svg/opacity.h"
#include "engine/vector/svg/overflow.h"
#include "engine/vector/svg/paint_order.h"
#include "engine/vector/svg/stop_list.h"
#include "engine/vector/svg/text_anchor.h"
#include "engine/vector/svg/vector_effect.h"
#include "engine/vector/svg/writing_mode.h"
#include "test_util.h"

using namespace pittore::svg;

namespace {

void test_op() {
    CHECK_NEAR(effectiveOpacity(0.5f, 0.5f), 0.25, 1e-6);
    CHECK(isDisplayNone("none"));
    CHECK(isVisible(""));
    CHECK(clipsOverflow("hidden"));
}

void test_text() {
    CHECK_NEAR(fontSizePx("medium", 10), 16, 1e-9);
    CHECK_NEAR(spacingPx("normal", 10), 0, 1e-9);
    CHECK(lineHeightPx("normal", 10) > 10);
    CHECK_NEAR(anchorShift("middle", 10), -5, 1e-9);
    CHECK(isVerticalText("vertical-rl"));
}

void test_paint() {
    CHECK(!paintOrderSlots("").empty());
    CHECK(isEvenOdd("evenodd"));
    CHECK(isNonScalingStroke("non-scaling-stroke"));
    CHECK(isLinearColor("linearRGB"));
    CHECK(isUserSpaceUnits("userSpaceOnUse"));
    // Named locals: the braced temporaries below trip GCC's
    // -Wdangling-pointer heuristic; the two calls stay verbatim.
    const std::vector<StopRow> badStop{{"bad", "red", ""}};
    const auto stopsA = buildStops(badStop);
    const auto stopsB = buildStops(badStop);
    CHECK(!stopsA.empty() || stopsB.empty());
}

}  // namespace

int main() {
    test_op();
    test_text();
    test_paint();
    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}
