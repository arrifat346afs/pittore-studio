// SVG final units: intake, raster, compose, cross-check, blur hash.
#include <string>

#include "engine/vector/svg/compose.h"
#include "engine/vector/svg/fe_blur.h"
#include "engine/vector/svg/import_bridge.h"
#include "engine/vector/svg/scene_image.h"
#include "engine/vector/svg/vector_scene_check.h"
#include "test_util.h"

using namespace pittore::svg;

namespace {

void test_intake() {
    const ImportedSvg s = importSvg(
        "<svg viewBox=\"0 0 100 50\"><rect x=\"1\" y=\"2\" width=\"10\" "
        "height=\"20\" fill=\"red\"/></svg>");
    CHECK(s.ok);
    CHECK(!s.items.empty());
    CHECK_NEAR(s.scene.viewW, 100, 1e-9);
}

void test_render() {
    const ImportedSvg s = importSvg(
        "<svg viewBox=\"0 0 10 10\"><rect x=\"2\" y=\"2\" width=\"6\" "
        "height=\"6\" fill=\"red\"/></svg>");
    CHECK(s.ok);
    const FeImg img = renderScene(s.scene, 1.0);
    CHECK_EQ(img.w, 10);
    CHECK_EQ(img.h, 10);
    const float inside = img.px[(size_t)((5 * 10 + 5) * 4 + 3)];
    const float outside = img.px[(size_t)((0 * 10 + 0) * 4 + 3)];
    CHECK(inside > 0.9f);
    CHECK(outside < 0.1f);
}

void test_cross() {
    const std::string xml =
        "<svg width=\"40\" height=\"20\"><rect x=\"1\" y=\"2\" width=\"10\" "
        "height=\"5\" fill=\"red\"/></svg>";
    CHECK(checkFlatParity(xml));
}

void test_blur_hash() {
    const FeImg a = makeFeImg(48, 48, 1, 0.5f, 0, 1);
    const FeImg b1 = feBlurBox(a, 2);
    const FeImg b2 = feBlurBox(a, 2);
    CHECK_EQ(b1.px.size(), b2.px.size());
    CHECK(b1.px == b2.px);
}

}  // namespace

int main() {
    test_intake();
    test_render();
    test_cross();
    test_blur_hash();
    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}
