// SVG batch3 units: paint, stroke, filter params, fe ops, refs.
#include <string>

#include "engine/vector/svg/fe_blur.h"
#include "engine/vector/svg/fe_buffer.h"
#include "engine/vector/svg/fe_color.h"
#include "engine/vector/svg/fe_composite.h"
#include "engine/vector/svg/filter_params.h"
#include "engine/vector/svg/image_ref.h"
#include "engine/vector/svg/paint_server.h"
#include "engine/vector/svg/parse_stats.h"
#include "engine/vector/svg/scene.h"
#include "engine/vector/svg/stroke_style.h"
#include "engine/vector/svg/svg_save.h"
#include "engine/vector/svg/use_expand.h"
#include "engine/vector/svg/validate.h"
#include "engine/vector/svg/xml_reader.h"
#include "test_util.h"

using namespace pittore::svg;

namespace {

void test_paint() {
    GradientStore gs;
    gs.byId["g"] = Gradient{false, 0, 0, 1, 0, 0.5, 0.5, 0.5,
                            {{0, 0, 0, 0, 1}, {1, 1, 1, 1, 1}}};
    Paint p;
    p.none = false;
    p.hasRef = true;
    p.ref = "g";
    float o[4] = {0, 0, 0, 0};
    CHECK(resolvePaint(p, gs, 0.5, o));
    CHECK(!resolvePaint(Paint{}, gs, 0, o));
}

void test_stroke() {
    CHECK(parseCap("round") == Cap::Round);
    CHECK(parseJoin("bevel") == Join::Bevel);
    const auto s = makeStroke(2, "round", "bevel", "4", "4 2", "1");
    CHECK_NEAR(s.width, 2, 1e-9);
    CHECK_EQ(s.dash.size(), 2u);
}

void test_params() {
    CHECK_NEAR(parseStdDev("2.5"), 2.5, 1e-9);
    const Flood f = parseFlood("#ff0000", "0.5");
    CHECK(f.a < 1);
    const Offset o = parseOffset("1", "2");
    CHECK_NEAR(o.dx, 1, 1e-9);
}

void test_fe() {
    FeImg a = makeFeImg(4, 4, 1, 0, 0, 1);
    CHECK_EQ(a.px.size(), 64u);
    CHECK_EQ(feBlurBox(a, 0).px.size(), 64u);
    CHECK_EQ(feBlurBox(a, 1).px.size(), 64u);
    const std::vector<double> m(20, 0);
    CHECK_EQ(feApplyMatrix(a, m).px.size(), 64u);
    CHECK_EQ(feLuminanceToAlpha(a).px.size(), 64u);
    FeImg b = makeFeImg(4, 4, 0, 0, 1, 1);
    CHECK_EQ(feComposite(CompOp::Over, a, b).px.size(), 64u);
    CHECK_EQ(feComposite(CompOp::In, a, b).px.size(), 64u);
    CHECK_EQ(feComposite(CompOp::Arithmetic, a, b, 0, 1, 0, 0).px.size(), 64u);
}

void test_refs() {
    CHECK(isDataUri("data:image/png;base64,x"));
    CHECK(isEmbeddedImage("data:image/png;base64,x"));
    CHECK(!isDataUri("#a"));
}

void test_use() {
    const XmlRead r = readXml("<svg><rect id=\"a\" width=\"4\"/><use href=\"#a\" x=\"3\"/></svg>");
    CHECK(r.ok);
    Scene s = buildScene(*r.root, {});
    CHECK(expandUses(*s.root) >= 1);
}

void test_save_stats() {
    const XmlRead r = readXml("<svg><rect/><use href=\"#a\"/></svg>");
    CHECK(r.ok);
    CHECK(!saveXml(*r.root).empty());
    const SvgStats st = collectStats(*r.root);
    CHECK_EQ(st.nodes, 3);
    CHECK_EQ(st.uses, 1);
}

void test_validate() {
    const XmlRead r = readXml("<svg><use href=\"#missing\"/><path d=\"\"/></svg>");
    CHECK(r.ok);
    const Scene s = buildScene(*r.root, {});
    CHECK(!validateScene(s).empty());
}

}  // namespace

int main() {
    test_paint();
    test_stroke();
    test_params();
    test_fe();
    test_refs();
    test_use();
    test_save_stats();
    test_validate();
    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}
