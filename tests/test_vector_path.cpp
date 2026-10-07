// test_vector_path.cpp — the path builder and the coverage rasterizer that
// shapes and text paint through: exact fills on axis-aligned geometry,
// antialiased partial coverage on diagonals, both fill rules, cubic
// flattening, stroke expansion and degenerate inputs.
#include <algorithm>
#include <cstdint>
#include <vector>

#include "engine/vector/path.h"
#include "test_util.h"

using namespace pittore::vector;

namespace {

std::uint8_t covAt(const std::vector<std::uint8_t>& mask, const IRect& rect,
                   int x, int y) {
    const int w = rect.width();
    return mask[static_cast<std::size_t>(y - rect.top) * w + (x - rect.left)];
}

void test_rect_fills_exactly() {
    PathBuilder b;
    b.rect(IRect::fromXYWH(2, 2, 6, 6));
    const Path path = b.build(0.25f);
    const IRect rect = IRect::fromSize(12, 12);
    const auto mask = rasterize(path, rect, FillRule::NonZero);
    CHECK_EQ(covAt(mask, rect, 4, 4), 255);  // interior
    CHECK_EQ(covAt(mask, rect, 0, 0), 0);    // outside
    CHECK_EQ(covAt(mask, rect, 7, 7), 255);  // last inside pixel
    CHECK_EQ(covAt(mask, rect, 8, 8), 0);    // first outside pixel
}

void test_diagonal_is_antialiased() {
    PathBuilder b;
    b.moveTo(0, 0).lineTo(16, 16).lineTo(0, 16).close();
    const Path path = b.build(0.25f);
    const IRect rect = IRect::fromSize(16, 16);
    const auto mask = rasterize(path, rect, FillRule::NonZero);
    CHECK(covAt(mask, rect, 2, 12) > 240);  // well inside
    CHECK_EQ(covAt(mask, rect, 12, 2), 0);  // well outside
    const std::uint8_t edge = covAt(mask, rect, 8, 8);
    CHECK(edge > 20 && edge < 235);  // partial on the diagonal
}

void test_ellipse_is_round_and_centered() {
    PathBuilder b;
    b.ellipse(IRect::fromSize(32, 32));
    const Path path = b.build(0.2f);
    const IRect rect = IRect::fromSize(32, 32);
    const auto mask = rasterize(path, rect, FillRule::NonZero);
    CHECK_EQ(covAt(mask, rect, 16, 16), 255);  // centre filled
    CHECK_EQ(covAt(mask, rect, 0, 0), 0);      // corner empty
    CHECK_EQ(covAt(mask, rect, 31, 31), 0);    // opposite corner empty
    CHECK(covAt(mask, rect, 16, 1) > 0);       // cardinal point on the boundary
}

void test_even_odd_punches_holes() {
    // Both rings wind the same way round, so only the parity rule sees a hole.
    PathBuilder b;
    b.rect(IRect::fromXYWH(0, 0, 20, 20));
    b.rect(IRect::fromXYWH(5, 5, 10, 10));
    const Path path = b.build(0.25f);
    const IRect rect = IRect::fromSize(20, 20);

    const auto eo = rasterize(path, rect, FillRule::EvenOdd);
    CHECK_EQ(covAt(eo, rect, 10, 10), 0);    // even-odd leaves a hole
    CHECK_EQ(covAt(eo, rect, 2, 2), 255);

    const auto nz = rasterize(path, rect, FillRule::NonZero);
    CHECK_EQ(covAt(nz, rect, 10, 10), 255);  // nonzero fills through
}

void test_cubic_flattening_tracks_the_curve() {
    PathBuilder b;
    b.moveTo(0, 0).cubicTo(0, 10, 10, 10, 10, 0).close();
    const Path path = b.build(0.1f);
    const auto& pts = path.subpaths[0];
    CHECK(pts.size() > 8);
    float apex = -1e30f;
    for (const auto& p : pts) apex = std::max(apex, p.second);
    // The apex is at t=0.5 => y = 7.5 for this control net.
    CHECK_NEAR(apex, 7.5, 0.3);
}

void test_stroke_covers_the_line_and_its_width() {
    PathBuilder b;
    b.moveTo(2, 8).lineTo(14, 8);
    const Path line = b.build(0.25f);
    const Path stroked = strokeToPath(line, 4.0f);
    const IRect rect = IRect::fromSize(16, 16);
    const auto mask = rasterize(stroked, rect, FillRule::NonZero);
    CHECK_EQ(covAt(mask, rect, 8, 8), 255);       // on the line
    CHECK(covAt(mask, rect, 8, 7) > 200);         // within half-width
    CHECK_EQ(covAt(mask, rect, 8, 13), 0);        // beyond the stroke
}

void test_empty_and_degenerate_paths_are_safe() {
    const IRect rect = IRect::fromSize(4, 4);
    const auto empty = rasterize(Path{}, rect, FillRule::NonZero);
    CHECK_EQ(empty.size(), 16u);
    for (std::uint8_t v : empty) CHECK_EQ(v, 0);
    PathBuilder b;
    b.moveTo(1, 1).lineTo(2, 2).close();  // two points: no area
    const auto mask = rasterize(b.build(0.25f), rect, FillRule::NonZero);
    for (std::uint8_t v : mask) CHECK_EQ(v, 0);
}

void test_bounds_cover_all_points() {
    PathBuilder b;
    b.ellipse(IRect::fromXYWH(-4, 10, 20, 8));
    const Path path = b.build(0.2f);
    const IRect bounds = path.bounds();
    CHECK(bounds.left <= -4 && bounds.right >= 16);
    CHECK(bounds.top <= 10 && bounds.bottom >= 18);
}

void test_rect_helpers() {
    const IRect a = IRect::fromXYWH(2, 3, 4, 5);
    CHECK_EQ(a.right, 6);
    CHECK_EQ(a.bottom, 8);
    CHECK_EQ(a.width(), 4);
    CHECK_EQ(a.height(), 5);
    CHECK(a.contains(2, 3));
    CHECK(!a.contains(6, 3));
    const IRect overlap = a.intersect(IRect::fromXYWH(4, 5, 4, 5));
    const IRect expectOverlap = IRect::fromXYWH(4, 5, 2, 3);
    CHECK(overlap == expectOverlap);
    const IRect span = a.unionWith(IRect::fromXYWH(10, 10, 2, 2));
    const IRect expectSpan = IRect{2, 3, 12, 12};
    CHECK(span == expectSpan);
    // An empty rect is the identity for union and collapses intersections.
    const IRect same = a.unionWith(IRect{});
    CHECK(same == a);
    CHECK(a.intersect(IRect{}).isEmpty());
}

void test_variable_width_tapers_the_stroke() {
    // Horizontal line, width 2 at the start growing to 6 at the end.
    PathBuilder b;
    b.moveTo(0, 8).lineTo(16, 8);
    const Path line = b.build(0.25f);
    StrokeStyle style(4.0f);
    style.cap = LineCap::Butt;
    const std::vector<std::vector<float>> widths{{2.0f, 6.0f}};
    const Path tapered =
        strokeVariable(line, widths, style, {}, 0.0f);
    const IRect rect = IRect::fromSize(20, 20);
    const auto mask = rasterize(tapered, rect, FillRule::NonZero);
    CHECK_EQ(covAt(mask, rect, 1, 8), 255);    // narrow end: covered
    CHECK_EQ(covAt(mask, rect, 1, 5), 0);      // narrow end: thin
    CHECK_EQ(covAt(mask, rect, 15, 8), 255);   // wide end: covered
    CHECK(covAt(mask, rect, 15, 5) > 200);     // wide end: tall
    CHECK_EQ(covAt(mask, rect, 15, 4), 0);     // ...but bounded
}

void test_variable_width_zero_pinches_and_dots() {
    // Width 0 at the start pinches the outline to a point there.
    PathBuilder b;
    b.moveTo(2, 8).lineTo(14, 8);
    const Path line = b.build(0.25f);
    StrokeStyle style(4.0f);
    style.cap = LineCap::Butt;
    const Path pinched =
        strokeVariable(line, {{0.0f, 4.0f}}, style, {}, 0.0f);
    const IRect rect = IRect::fromSize(16, 16);
    const auto mask = rasterize(pinched, rect, FillRule::NonZero);
    CHECK_EQ(covAt(mask, rect, 13, 8), 255);
    // Dots pattern {0, 4} on the (2,8)-(14,8) line: dots at x=2,6,10,14.
    StrokeStyle dots(2.0f);
    dots.cap = LineCap::Round;
    const Path dotted =
        strokeVariable(line, {{2.0f, 2.0f}}, dots, {0.0f, 4.0f}, 0.0f);
    const auto dm = rasterize(dotted, rect, FillRule::NonZero);
    CHECK(covAt(dm, rect, 6, 8) > 100);    // dot centre
    CHECK_EQ(covAt(dm, rect, 4, 8), 0);    // gap between dots
    CHECK(covAt(dm, rect, 10, 8) > 100);   // next dot
    CHECK_EQ(covAt(dm, rect, 8, 8), 0);    // gap again
    // Same pattern with butt caps draws nothing (zero-length dashes).
    StrokeStyle butt(2.0f);
    butt.cap = LineCap::Butt;
    const Path butted =
        strokeVariable(line, {{2.0f, 2.0f}}, butt, {0.0f, 4.0f}, 0.0f);
    const auto bm = rasterize(butted, rect, FillRule::NonZero);
    for (std::uint8_t v : bm) CHECK_EQ(v, 0);
}

void test_variable_width_dash_splits_and_profile() {
    // Dash {4, 4} on a 16-long line: ink, gap, ink.
    PathBuilder b;
    b.moveTo(0, 8).lineTo(16, 8);
    const Path line = b.build(0.25f);
    StrokeStyle style(2.0f);
    style.cap = LineCap::Butt;
    const Path dashed =
        strokeVariable(line, {{2.0f, 2.0f}}, style, {4.0f, 4.0f}, 0.0f);
    const IRect rect = IRect::fromSize(20, 12);
    const auto mask = rasterize(dashed, rect, FillRule::NonZero);
    CHECK_EQ(covAt(mask, rect, 2, 8), 255);
    CHECK_EQ(covAt(mask, rect, 6, 8), 0);
    CHECK_EQ(covAt(mask, rect, 10, 8), 255);
    // Profile interpolation: midpoint of 0→1 with ends 2 and 6 is 4.
    WidthProfile prof;
    prof.pos = {0.0f, 1.0f};
    prof.scale = {0.5f, 1.5f};
    CHECK_NEAR(widthProfileAt(prof, 0.0f), 0.5f, 1e-6);
    CHECK_NEAR(widthProfileAt(prof, 0.5f), 1.0f, 1e-6);
    CHECK_NEAR(widthProfileAt(prof, 1.0f), 1.5f, 1e-6);
    CHECK_NEAR(widthProfileAt(WidthProfile{}, 0.3f), 1.0f, 1e-6);
    // flattenSegments matches PathBuilder for mixed segments.
    PathBuilder c;
    c.moveTo(0, 0).lineTo(10, 0).cubicTo(12, 0, 14, 2, 16, 2).close();
    const Path a = c.build(0.25f);
    PathBuilder d;
    d.segments = c.segments;
    const Path e = flattenSegments(d.segments, 0.25f);
    CHECK_EQ(a.subpaths.size(), e.subpaths.size());
    CHECK_EQ(a.closed.size(), e.closed.size());
}

void run() {
    test_rect_fills_exactly();
    test_diagonal_is_antialiased();
    test_ellipse_is_round_and_centered();
    test_even_odd_punches_holes();
    test_cubic_flattening_tracks_the_curve();
    test_stroke_covers_the_line_and_its_width();
    test_empty_and_degenerate_paths_are_safe();
    test_bounds_cover_all_points();
    test_rect_helpers();
    test_variable_width_tapers_the_stroke();
    test_variable_width_zero_pinches_and_dots();
    test_variable_width_dash_splits_and_profile();
}

}  // namespace

TEST_MAIN_CALL(run)
