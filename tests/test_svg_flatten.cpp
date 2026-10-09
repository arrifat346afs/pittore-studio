// Flatten-fallback size gate: the single-raster rescue for pathological
// SVGs must refuse what the machine cannot rasterize, and nothing else.
// Nova Scotia (9900x8000 = 79.2MP) opens on any machine whose scaled area
// budget covers it, instead of dying on a hardcoded 64MP cap.
#include "test_util.h"
#include "ui/svg_parts.h"

using namespace pittore::ui;

namespace {

void test_size_gate() {
    const qint64 tiny = 64ll * 1024 * 1024;
    const qint64 scaled = 512ll * 1024 * 1024;
    const qint64 huge = (1ll << 60);
    // Degenerate and over-wide canvases never rasterize, whatever the budget.
    CHECK(!svgFlattenSizeOk(0, 100, huge));
    CHECK(!svgFlattenSizeOk(-40, 100, huge));
    CHECK(!svgFlattenSizeOk(100, 0, huge));
    CHECK(!svgFlattenSizeOk(16385, 100, huge));
    CHECK(!svgFlattenSizeOk(100, 20000, huge));
    CHECK(svgFlattenSizeOk(16384, 16384, huge));
    CHECK(svgFlattenSizeOk(100, 100, tiny));
    // The Nova Scotia canvas (79.2MP): refused under the small budget the
    // old hardcoded cap behaved like, accepted under a scaled one.
    CHECK(!svgFlattenSizeOk(9900, 8000, tiny));
    CHECK(svgFlattenSizeOk(9900, 8000, scaled));
    // Side cap binds before pixels: a 268MP square needs a big budget.
    CHECK(!svgFlattenSizeOk(16384, 16384, tiny));
}

}  // namespace

int main() {
    test_size_gate();
    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}
