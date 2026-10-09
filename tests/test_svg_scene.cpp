// SVG scene units: build, style, refs.
#include <string>

#include "engine/vector/svg/css_parse.h"
#include "engine/vector/svg/scene.h"
#include "engine/vector/svg/xml_reader.h"
#include "test_util.h"

using namespace pittore::svg;

namespace {

Scene makeScene(const std::string& xml) {
    const XmlRead r = readXml(xml);
    CHECK(r.ok);
    std::vector<CssRule> sheet;
    // Collect style blocks like the DOM pass does.
    for (const auto& c : r.root->children) {
        if (c->tag == "style") {
            for (const auto& q : parseStylesheet(c->text)) {
                sheet.push_back(q);
            }
        }
    }
    return buildScene(*r.root, sheet);
}

void test_shapes() {
    const Scene s = makeScene(
        "<svg viewBox=\"0 0 100 50\"><rect x=\"1\" y=\"2\" width=\"10\" "
        "height=\"20\"/><circle cx=\"5\" cy=\"5\" r=\"3\"/><path d=\"M0 0 L1 "
        "1\"/></svg>");
    CHECK(s.ok);
    CHECK_NEAR(s.viewW, 100, 1e-9);
    CHECK_NEAR(s.viewH, 50, 1e-9);
    CHECK_EQ(s.root->children.size(), 3u);
    CHECK(s.root->children[0]->kind == NodeKind::Rect);
    CHECK_NEAR(s.root->children[0]->w, 10, 1e-9);
    CHECK(s.root->children[2]->kind == NodeKind::Path);
    CHECK(!s.root->children[2]->segs.empty());
}

void test_inherit() {
    const Scene s = makeScene(
        "<svg><g fill=\"#ff0000\"><rect width=\"5\"/></g></svg>");
    CHECK(s.ok);
    CHECK(!s.root->children[0]->children[0]->style.fill.none);
}

void test_sheet() {
    const Scene s = makeScene(
        "<svg><style>.a{fill:#00ff00}</style><rect class=\"a\" "
        "width=\"5\"/></svg>");
    CHECK(s.ok);
    CHECK_EQ(s.root->children.size(), 1u);
    CHECK(!s.root->children[0]->style.fill.none);
}

void test_xform() {
    const Scene s = makeScene(
        "<svg><g transform=\"translate(10 0)\"><rect x=\"1\" "
        "width=\"2\"/></g></svg>");
    CHECK(s.ok);
    CHECK_NEAR(s.root->children[0]->children[0]->world.e, 10, 1e-9);
}

void test_use() {
    const Scene s = makeScene(
        "<svg><rect id=\"a\" width=\"4\"/><use href=\"#a\" x=\"3\"/></svg>");
    CHECK(s.ok);
    CHECK(s.root->children[1]->kind == NodeKind::Use);
    CHECK(s.root->children[1]->href == "a");
}

}  // namespace

int main() {
    test_shapes();
    test_inherit();
    test_sheet();
    test_xform();
    test_use();
    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}
