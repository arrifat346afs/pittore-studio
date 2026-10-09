// SVG batch5 units: second half of value helpers.
#include "engine/vector/svg/circle_args.h"
#include "engine/vector/svg/clip_units.h"
#include "engine/vector/svg/dxdy_list.h"
#include "engine/vector/svg/ellipse_args.h"
#include "engine/vector/svg/id_index.h"
#include "engine/vector/svg/image_fit.h"
#include "engine/vector/svg/inherit_flag.h"
#include "engine/vector/svg/lang_space.h"
#include "engine/vector/svg/line_args.h"
#include "engine/vector/svg/mask_units.h"
#include "engine/vector/svg/path_length.h"
#include "engine/vector/svg/pattern_units.h"
#include "engine/vector/svg/poly_points.h"
#include "engine/vector/svg/rect_args.h"
#include "engine/vector/svg/rotate_list.h"
#include "engine/vector/svg/unknown_keep.h"
#include "engine/vector/svg/xml_reader.h"
#include "test_util.h"

using namespace pittore::svg;

namespace {

void test_geom() {
    const RectArgs r = parseRectArgs("1", "2", "10", "20", "3", "", 100, 100);
    CHECK_NEAR(r.w, 10, 1e-9);
    CHECK_NEAR(r.ry, 3, 1e-9);
    const CircleArgs c = parseCircleArgs("5", "5", "3", 100, 100);
    CHECK_NEAR(c.r, 3, 1e-9);
    const EllipseArgs e = parseEllipseArgs("1", "2", "3", "4", 100, 100);
    CHECK_NEAR(e.ry, 4, 1e-9);
    const LineArgs l = parseLineArgs("0", "0", "5", "5", 100, 100);
    CHECK_NEAR(l.x2, 5, 1e-9);
    CHECK_EQ(parsePolyPoints("0,0 10,0 10,10").size(), 3u);
    CHECK_NEAR(pathLengthScale("", 10), 1, 1e-9);
}

void test_lists() {
    CHECK_EQ(parseRotateList("10 20").size(), 2u);
    CHECK_EQ(parsePosList("1 2", 100).size(), 2u);
    CHECK(preservesSpace("preserve"));
    CHECK(clipIsUserSpace(""));
    CHECK(maskIsAlpha("alpha"));
    CHECK(patternIsUserSpace("userSpaceOnUse"));
    CHECK(inheritsProp("fill"));
    CHECK(!inheritsProp("d"));
}

void test_tree() {
    const XmlRead r = readXml("<svg><rect id=\"a\"/></svg>");
    CHECK(r.ok);
    CHECK(!indexIds(*r.root).empty());
    const FitRect f = fitView(10, 10, 0, 0, 20, 20, false, false);
    CHECK_NEAR(f.w, 20, 1e-9);
    CHECK(isKnownAttr("rect", "x"));
    CHECK(!unknownAttrs("rect", {{"x", "1"}, {"foo-bar", "2"}}).empty());
}

}  // namespace

int main() {
    test_geom();
    test_lists();
    test_tree();
    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}
