// SVG value units: length, transform, path data, paint.
#include <string>

#include "engine/vector/svg/color_parse.h"
#include "engine/vector/svg/length.h"
#include "engine/vector/svg/path_data.h"
#include "engine/vector/svg/transform.h"
#include "test_util.h"

using namespace pittore::svg;

namespace {

void test_length() {
    CHECK(parseLength("10").valid);
    CHECK_NEAR(toPx(parseLength("10"), 200), 10, 1e-9);
    CHECK_NEAR(toPx(parseLength("50%"), 200), 100, 1e-9);
    CHECK_NEAR(toPx(parseLength("2in"), 0), 192, 1e-9);
    CHECK_NEAR(toPx(parseLength("12pt"), 0), 16, 1e-9);
    CHECK(!parseLength("10xx").valid);
    CHECK_NEAR(readLength("bad", 0, 3), 3, 1e-9);
}

void test_transform() {
    const Affine t = parseTransform("translate(10 20) scale(2)");
    CHECK_NEAR(t.e, 10, 1e-9);
    CHECK_NEAR(t.a, 2, 1e-9);
    const Affine r = parseTransform("rotate(90)");
    CHECK_NEAR(r.b, 1, 1e-9);
    CHECK(isIdentity(parseTransform("bogus(1)")));
    CHECK(writeTransform(identity()).empty());
}

void test_path() {
    const auto v = parsePathData("M0 0 L10 0 H5 V5 C0 0 1 1 2 2 Z");
    CHECK(!v.empty());
    CHECK(v[0].type == SegType::Move);
    CHECK(v[1].type == SegType::Line);
    CHECK(v.back().type == SegType::Close);
    const auto rel = parsePathData("m0 0 l1 1");
    CHECK(rel[1].rel);
    CHECK(parsePathData("M0 0 L").size() == 1u);
}

void test_paint() {
    float c[4] = {0, 0, 0, 0};
    CHECK(parseColor("#ff0000", c));
    CHECK(c[0] > 0.9f);
    CHECK(!parseColor("none", c));
    CHECK(parseColor("rgb(255,0,0)", c));
    CHECK(parseColor("hsl(0,100%,50%)", c));
    Paint p = parsePaint("url(#g1)");
    CHECK(!p.none && p.hasRef && p.ref == "g1");
    CHECK(parsePaint("none").none);
}

}  // namespace

int main() {
    test_length();
    test_transform();
    test_path();
    test_paint();
    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}
