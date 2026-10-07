// test_vector_shape.cpp — the anchor model, the live-shape geometry builders
// and shape rasterization. The checks pin the invariants the shape decoder
// relies on: exact bounds, the expected anchor counts and positions, the two
// fill rules, stroke coverage, and gradient shading.
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "engine/vector/vector_shape.h"
#include "test_util.h"

using namespace pittore::vector;

namespace {

const std::uint8_t* sample(const ShapeImage& img, int x, int y) {
    return &img.rgba[(static_cast<std::size_t>(y - img.rect.top) * img.rect.width() +
                      (x - img.rect.left)) *
                     4];
}

VectorShape rectShape(float x0, float y0, float x1, float y1,
                      std::array<std::uint8_t, 4> fill) {
    VectorShape s;
    s.fill = fill;
    SubPath sub;
    sub.closed = true;
    sub.anchors = {Anchor::corner(x0, y0), Anchor::corner(x1, y0),
                   Anchor::corner(x1, y1), Anchor::corner(x0, y1)};
    s.path.subpaths.push_back(sub);
    return s;
}

void test_ellipse_anchors_sit_on_the_box() {
    const auto a = ellipseAnchors(0, 0, 20, 10);
    CHECK_EQ(a.size(), 4u);
    CHECK_NEAR(a[0].px, 10.0, 1e-4);
    CHECK_NEAR(a[0].py, 0.0, 1e-4);
    CHECK_NEAR(a[1].px, 20.0, 1e-4);
    CHECK_NEAR(a[1].py, 5.0, 1e-4);
    CHECK_NEAR(a[2].px, 10.0, 1e-4);
    CHECK_NEAR(a[2].py, 10.0, 1e-4);
    CHECK_NEAR(a[3].px, 0.0, 1e-4);
    CHECK_NEAR(a[3].py, 5.0, 1e-4);
    // A smooth anchor mirrors its handles.
    CHECK_NEAR(a[1].hix, -a[1].hox, 1e-4);
    CHECK_NEAR(a[1].hiy, -a[1].hoy, 1e-4);
}

void test_cornered_rect_anchors() {
    const std::array<float, 4> sharp{0, 0, 0, 0};
    const auto square = corneredRectAnchors(0, 0, 10, 10, sharp, {0, 0, 0, 0});
    CHECK_EQ(square.size(), 4u);
    CHECK_NEAR(square[0].px, 0.0, 1e-4);
    CHECK_NEAR(square[0].py, 0.0, 1e-4);
    CHECK_NEAR(square[2].px, 10.0, 1e-4);
    CHECK_NEAR(square[2].py, 10.0, 1e-4);

    const std::array<float, 4> rounded{3, 3, 3, 3};
    const auto round = corneredRectAnchors(0, 0, 10, 10, rounded, {0, 0, 0, 0});
    CHECK_EQ(round.size(), 8u);
    // The top-left corner contributes its entry anchor (a radius down the
    // left edge) then its exit anchor (a radius along the top edge).
    CHECK_NEAR(round[0].px, 0.0, 1e-4);
    CHECK_NEAR(round[0].py, 3.0, 1e-4);
    CHECK_NEAR(round[1].px, 3.0, 1e-4);
    CHECK_NEAR(round[1].py, 0.0, 1e-4);

    // A straight chamfer replaces the arc with two corners.
    const auto chamfer = corneredRectAnchors(0, 0, 10, 10, rounded, {1, 1, 1, 1});
    CHECK_EQ(chamfer.size(), 8u);
    CHECK_NEAR(chamfer[0].px, 0.0, 1e-4);
    CHECK_NEAR(chamfer[0].py, 3.0, 1e-4);
    CHECK_NEAR(chamfer[1].px, 3.0, 1e-4);
    CHECK_NEAR(chamfer[1].py, 0.0, 1e-4);
}

void test_unit_anchor_maps_onto_the_box() {
    const Anchor a = unitAnchor(1.0f, -1.0f, 0, 0, 20, 10);
    CHECK_NEAR(a.px, 20.0, 1e-4);
    CHECK_NEAR(a.py, 0.0, 1e-4);
    const Anchor c = unitAnchor(0.0f, 0.0f, 0, 0, 20, 10);
    CHECK_NEAR(c.px, 10.0, 1e-4);
    CHECK_NEAR(c.py, 5.0, 1e-4);
}

void test_arc_anchors_split_into_quarters() {
    const auto a = arcAnchors(0, 0, 10, 10, 0.0f, 3.14159265f);
    CHECK_EQ(a.size(), 3u);  // half circle => two 90-degree cubics
    CHECK_NEAR(a.front().px, 10.0, 1e-4);
    CHECK_NEAR(a.front().py, 0.0, 1e-4);
    CHECK_NEAR(a.back().px, -10.0, 1e-4);
    CHECK_NEAR(a.back().py, 0.0, 1e-4);
    CHECK_NEAR(a[1].px, 0.0, 1e-4);
    CHECK_NEAR(a[1].py, 10.0, 1e-4);
}

void test_star_and_cloud_anchor_counts() {
    CHECK_EQ(squareStarAnchors(4, 0.5f, 0, 0, 20, 20).size(), 12u);
    CHECK_EQ(cloudAnchors(12, 0.8f, 0, 0, 20, 20).size(), 24u);
    CHECK_EQ(heartAnchors(0, 0, 20, 20, 0.2f).size(), 6u);
}

void test_circle_through_three_points() {
    const auto c = circleThrough({0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f});
    CHECK(c.has_value());
    CHECK_NEAR(c->first.first, 0.5, 1e-4);
    CHECK_NEAR(c->first.second, 0.5, 1e-4);
    CHECK_NEAR(c->second, 0.70710678, 1e-4);
    // Collinear points have no circle.
    CHECK(!circleThrough({0.0f, 0.0f}, {1.0f, 0.0f}, {2.0f, 0.0f}).has_value());
}

void test_subpath_from_records() {
    // An on-curve point, an outgoing control, then the next on-curve point.
    const std::vector<PathRecord> records = {
        {0.0f, 0.0f, 1, 0}, {5.0f, -5.0f, 0, 1},
        {10.0f, 0.0f, 0, 2}, {10.0f, 10.0f, 2, 0},
        {0.0f, 10.0f, 0, 0},
    };
    const auto sub = subpathFromRecords(records, false);
    CHECK(sub.has_value());
    CHECK_EQ(sub->anchors.size(), 3u);
    CHECK_NEAR(sub->anchors[0].hox, 5.0, 1e-4);
    CHECK_NEAR(sub->anchors[0].hoy, -5.0, 1e-4);
    CHECK_NEAR(sub->anchors[1].hix, 0.0, 1e-4);
    CHECK_NEAR(sub->anchors[1].hiy, -10.0, 1e-4);
    CHECK(sub->anchors[1].hox == 0.0f);
}

void test_solid_shape_fills_and_clears() {
    const VectorShape shape = rectShape(0, 0, 10, 10, {255, 0, 0, 255});
    const auto img = rasterizeShape(shape, nullptr);
    CHECK(img.has_value());
    const std::uint8_t* inside = sample(*img, 5, 5);
    CHECK_EQ(inside[0], 255);
    CHECK_EQ(inside[1], 0);
    CHECK_EQ(inside[2], 0);
    CHECK_EQ(inside[3], 255);
    CHECK_EQ(sample(*img, -1, -1)[3], 0);
    CHECK_EQ(sample(*img, 11, 11)[3], 0);
}

void test_even_odd_punches_a_hole() {
    VectorShape shape;
    shape.fill = {0, 0, 255, 255};
    shape.evenOdd = true;
    SubPath outer;
    outer.closed = true;
    outer.anchors = {Anchor::corner(0, 0), Anchor::corner(20, 0),
                     Anchor::corner(20, 20), Anchor::corner(0, 20)};
    SubPath inner;
    inner.closed = true;
    inner.anchors = {Anchor::corner(5, 5), Anchor::corner(15, 5),
                     Anchor::corner(15, 15), Anchor::corner(5, 15)};
    shape.path.subpaths = {outer, inner};
    const auto img = rasterizeShape(shape, nullptr);
    CHECK(img.has_value());
    CHECK_EQ(sample(*img, 10, 10)[3], 0);
    CHECK_EQ(sample(*img, 2, 10)[3], 255);
}

void test_stroke_covers_the_line() {
    VectorShape shape;
    shape.fill = {0, 0, 0, 0};
    shape.hasStroke = true;
    shape.stroke = {0, 255, 0, 255};
    shape.strokeWidth = 4.0f;
    SubPath line;
    line.closed = false;
    line.anchors = {Anchor::corner(2, 8), Anchor::corner(14, 8)};
    shape.path.subpaths.push_back(line);
    const auto img = rasterizeShape(shape, nullptr);
    CHECK(img.has_value());
    CHECK_EQ(sample(*img, 8, 8)[1], 255);
    CHECK_EQ(sample(*img, 8, 5)[3], 0);  // beyond the stroke width
}

void test_linear_gradient_shades_along_the_axis() {
    VectorShape shape = rectShape(0, 0, 10, 10, {0, 0, 0, 255});
    GradientFill grad;
    grad.stops = {{0.0f, {0, 0, 0, 255}}, {1.0f, {255, 255, 255, 255}}};
    grad.startX = 0.0;
    grad.startY = 0.0;
    grad.endX = 10.0;
    grad.endY = 0.0;
    const auto img = rasterizeShape(shape, &grad);
    CHECK(img.has_value());
    const std::uint8_t* left = sample(*img, 0, 5);
    const std::uint8_t* right = sample(*img, 9, 5);
    CHECK(left[3] > 0);
    CHECK(right[3] > 0);
    CHECK(right[0] > left[0]);
    CHECK(left[0] < 40);
    CHECK(right[0] > 215);
}

void run() {
    test_ellipse_anchors_sit_on_the_box();
    test_cornered_rect_anchors();
    test_unit_anchor_maps_onto_the_box();
    test_arc_anchors_split_into_quarters();
    test_star_and_cloud_anchor_counts();
    test_circle_through_three_points();
    test_subpath_from_records();
    test_solid_shape_fills_and_clears();
    test_even_odd_punches_a_hole();
    test_stroke_covers_the_line();
    test_linear_gradient_shades_along_the_axis();
}

}  // namespace

TEST_MAIN_CALL(run)
