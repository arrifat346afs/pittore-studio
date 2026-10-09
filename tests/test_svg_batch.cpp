// SVG batch units: iri, view, gradient, items, bounds, emit, stripes.
#include <string>

#include "engine/vector/svg/bounds.h"
#include "engine/vector/svg/css_parse.h"
#include "engine/vector/svg/export_svg.h"
#include "engine/vector/svg/gradient.h"
#include "engine/vector/svg/iri.h"
#include "engine/vector/svg/png_stripes.h"
#include "engine/vector/svg/render_item.h"
#include "engine/vector/svg/scene.h"
#include "engine/vector/svg/viewbox.h"
#include "engine/vector/svg/xml_reader.h"
#include "test_util.h"

using namespace pittore::svg;

namespace {

Scene doc(const std::string& xml) {
    const XmlRead r = readXml(xml);
    CHECK(r.ok);
    return buildScene(*r.root, {});
}

void test_iri() {
    CHECK(refTarget("url(#a)") == "a");
    CHECK(refTarget("#b") == "");
    CHECK(normalizeIri("#c") == "c");
    CHECK(normalizeIri("  #d  ") == "d");
}

void test_view() {
    double w = 0, h = 0;
    CHECK(parseViewBox("0 0 40 20", w, h));
    CHECK_NEAR(w, 40, 1e-9);
    const Viewport v = resolveViewport("0 0 40 20", "10", "10");
    CHECK_NEAR(v.w, 40, 1e-9);
    const Viewport d = resolveViewport("", "bad", "bad");
    CHECK_NEAR(d.w, 1000, 1e-9);
}

void test_grad() {
    float o[4] = {0, 0, 0, 0};
    sampleStops({}, 0.5, o);
    CHECK_NEAR(o[3], 1, 1e-9);
    const std::vector<GradStop> s{{0, 0, 0, 0, 0}, {1, 1, 1, 1, 1}};
    sampleStops(s, 0.5, o);
    CHECK_NEAR(o[0], 0.5, 1e-9);
    sampleStops(s, -1, o);
    CHECK_NEAR(o[0], 0, 1e-9);
}

void test_items() {
    const Scene s = doc("<svg><g display=\"none\"><rect width=\"5\"/></g><rect x=\"1\" width=\"2\"/></svg>");
    const auto items = flattenScene(s);
    CHECK(!items.empty());
    int rects = 0;
    for (const auto& it : items) {
        if (it.kind == ItemKind::Rect) {
            ++rects;
        }
    }
    CHECK_EQ(rects, 1);
}

void test_bounds() {
    const Scene s = doc("<svg><rect x=\"1\" y=\"2\" width=\"10\" height=\"20\"/></svg>");
    const auto items = flattenScene(s);
    BBox all;
    for (const auto& it : items) {
        all = unionBox(all, itemBounds(it));
    }
    CHECK(!all.empty);
    CHECK_NEAR(all.x0, 1, 1e-9);
    CHECK_NEAR(all.x1, 11, 1e-9);
}

void test_emit() {
    const Scene s = doc("<svg viewBox=\"0 0 10 10\"><rect width=\"5\"/><path d=\"M0 0 L1 1\"/></svg>");
    const std::string svg = emitSceneSvg(s);
    CHECK(svg.find("<svg") != std::string::npos);
    CHECK(svg.find("<rect") != std::string::npos);
    CHECK(svg.find("<path") != std::string::npos);
}

void test_stripes() {
    const auto v = splitStripes(130);
    CHECK_EQ(v.size(), 3u);
    CHECK_EQ(v[0].h, 64);
    CHECK_EQ(v[2].y0, 128);
    CHECK(splitStripes(0).empty());
}

}  // namespace

int main() {
    test_iri();
    test_view();
    test_grad();
    test_items();
    test_bounds();
    test_emit();
    test_stripes();
    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}
