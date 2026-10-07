// test_vector_svg.cpp — the retained vector-art model and the SVG writer.
// Checks the base64 payloads, the binary round-trip used by project files, and
// the SVG text itself: real paths (lines, cubics, closes), gradients in <defs>,
// strokes, transforms, and embedded raster <image> pieces.
#include <cstdint>
#include <string>
#include <vector>

#include "engine/vector/vector_art.h"
#include "engine/vector/vector_shape.h"
#include "test_util.h"

using namespace pittore::vector;

namespace {

Segment move(float x, float y) {
    Segment s;
    s.kind = Segment::Kind::MoveTo;
    s.x = x;
    s.y = y;
    return s;
}
Segment line(float x, float y) {
    Segment s;
    s.kind = Segment::Kind::LineTo;
    s.x = x;
    s.y = y;
    return s;
}
Segment cubic(float c1x, float c1y, float c2x, float c2y, float x, float y) {
    Segment s;
    s.kind = Segment::Kind::CubicTo;
    s.c1x = c1x;
    s.c1y = c1y;
    s.c2x = c2x;
    s.c2y = c2y;
    s.x = x;
    s.y = y;
    return s;
}
Segment close() {
    Segment s;
    s.kind = Segment::Kind::Close;
    return s;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

void test_base64_vectors() {
    auto enc = [](const char* s) {
        return base64Encode(reinterpret_cast<const std::uint8_t*>(s),
                            std::char_traits<char>::length(s));
    };
    CHECK(enc("").empty());
    CHECK(enc("f") == std::string("Zg=="));
    CHECK(enc("fo") == std::string("Zm8="));
    CHECK(enc("foo") == std::string("Zm9v"));
    CHECK(enc("foob") == std::string("Zm9vYg=="));
    CHECK(enc("fooba") == std::string("Zm9vYmE="));
    CHECK(enc("foobar") == std::string("Zm9vYmFy"));
}

ArtNode richNode() {
    ArtNode n;
    n.name = "circle";
    n.segments = {move(0, 0), cubic(0, 10, 10, 10, 10, 0), cubic(0, -10, -10, -10, 0, 0),
                  close()};
    n.matrix[0] = 2.0;
    n.matrix[3] = 3.0;
    n.matrix[4] = 5.0;
    n.matrix[5] = 7.0;
    n.opacity = 0.5;
    n.evenOdd = true;
    n.paint.hasFill = true;
    n.paint.fill[0] = 0x11;
    n.paint.fill[1] = 0x22;
    n.paint.fill[2] = 0x33;
    n.paint.fill[3] = 0x80;
    n.paint.hasGradient = true;
    n.paint.gradient.radial = true;
    n.paint.gradient.cx = 0.25;
    n.paint.gradient.stops = {{0.0f, {255, 0, 0, 255}}, {1.0f, {0, 0, 255, 128}}};
    n.paint.hasStroke = true;
    n.paint.stroke[0] = 0xaa;
    n.paint.strokeWidth = 2.5;
    n.paint.cap = 1;
    n.paint.join = 2;
    return n;
}

void test_art_node_round_trip() {
    const ArtNode in = richNode();
    const std::vector<std::uint8_t> blob = encodeArtNode(in);
    CHECK(!blob.empty());
    std::size_t consumed = 0;
    const auto out = decodeArtNode(blob, &consumed);
    CHECK(out.has_value());
    if (!out) return;
    CHECK_EQ(consumed, blob.size());
    CHECK(out->name == in.name);
    CHECK_EQ(out->segments.size(), in.segments.size());
    CHECK_EQ(static_cast<int>(out->segments[1].kind),
             static_cast<int>(Segment::Kind::CubicTo));
    CHECK_NEAR(out->segments[1].c1y, 10.0f, 1e-6);
    CHECK_NEAR(out->segments[1].x, 10.0f, 1e-6);
    CHECK_EQ(static_cast<int>(out->segments.back().kind),
             static_cast<int>(Segment::Kind::Close));
    CHECK(out->evenOdd);
    CHECK_EQ(out->opacity, in.opacity);
    CHECK_NEAR(out->matrix[4], 5.0, 1e-9);
    CHECK_EQ(static_cast<int>(out->paint.fill[0]), 0x11);
    CHECK_EQ(static_cast<int>(out->paint.fill[3]), 0x80);
    CHECK(out->paint.hasGradient);
    CHECK(out->paint.gradient.radial);
    CHECK_EQ(out->paint.gradient.stops.size(), 2u);
    CHECK_EQ(static_cast<int>(out->paint.gradient.stops[1].rgba[3]), 128);
    CHECK(out->paint.hasStroke);
    CHECK_NEAR(out->paint.strokeWidth, 2.5, 1e-9);
    CHECK_EQ(out->paint.cap, 1);
    CHECK_EQ(out->paint.join, 2);
}

void test_truncated_blob_is_rejected() {
    const std::vector<std::uint8_t> blob = encodeArtNode(richNode());
    for (std::size_t cut = 0; cut < blob.size(); cut += 7) {
        std::vector<std::uint8_t> sliced(blob.begin(), blob.begin() + cut);
        auto out = decodeArtNode(sliced);
        if (!out) continue;  // rejected outright — the common case
        // Accepted only at clean tail-section boundaries (forward-compat
        // tails read as absent): re-encoding must reproduce the prefix.
        const std::vector<std::uint8_t> canon = encodeArtNode(*out);
        CHECK(canon.size() >= cut);
        if (canon.size() < cut) continue;
        CHECK(std::equal(sliced.begin(), sliced.end(), canon.begin()));
    }
}

void test_document_writes_real_vector() {
    ArtDocument doc;
    doc.width = 200.0;
    doc.height = 100.0;

    ArtElement a;
    a.node.name = "box";
    a.node.segments = {move(0, 0), line(10, 0), line(10, 10), line(0, 10), close()};
    a.node.paint.hasFill = true;
    a.node.paint.fill[0] = 0x11;
    a.node.paint.fill[1] = 0x22;
    a.node.paint.fill[2] = 0x33;
    a.node.paint.hasStroke = true;
    a.node.paint.stroke[0] = 0xff;
    a.node.paint.strokeWidth = 1.5;
    a.node.matrix[4] = 4.0;
    a.node.evenOdd = true;
    doc.elements.push_back(a);

    ArtElement g;
    g.node.segments = {move(0, 0), cubic(0, 5, 5, 5, 5, 0), close()};
    g.node.paint.hasFill = true;
    g.node.paint.hasGradient = true;
    g.node.paint.gradient.stops = {{0.0f, {0, 0, 0, 255}}, {1.0f, {255, 255, 255, 255}}};
    doc.elements.push_back(g);

    ArtElement r;
    r.raster = true;
    r.image.x = 1.0;
    r.image.y = 2.0;
    r.image.w = 30.0;
    r.image.h = 40.0;
    r.image.base64Png = base64Encode(reinterpret_cast<const std::uint8_t*>("png"), 3);
    doc.elements.push_back(r);

    const std::string svg = artDocumentToSvg(doc);
    CHECK(!svg.empty());
    CHECK(contains(svg, "<svg "));
    CHECK(contains(svg, "width=\"200\""));
    CHECK(contains(svg, "height=\"100\""));
    CHECK(contains(svg, "viewBox=\"0 0 200 100\""));
    CHECK(contains(svg, "<path "));
    CHECK(contains(svg, "d=\"M0 0 L10 0 L10 10 L0 10 Z\""));
    CHECK(contains(svg, "fill=\"#112233\""));
    CHECK(contains(svg, "stroke=\"#ff0000\""));
    CHECK(contains(svg, "stroke-width=\"1.5\""));
    CHECK(contains(svg, "transform=\"matrix(1,0,0,1,4,0)\""));
    CHECK(contains(svg, "fill-rule=\"evenodd\""));
    CHECK(contains(svg, "<linearGradient"));
    CHECK(contains(svg, "gradientUnits=\"userSpaceOnUse\""));
    CHECK(contains(svg, "fill=\"url(#grad0)\""));
    CHECK(contains(svg, "<stop offset=\"0\" stop-color=\"#000000\""));
    CHECK(contains(svg, "<image "));
    CHECK(contains(svg, "x=\"1\" y=\"2\" width=\"30\" height=\"40\""));
    CHECK(contains(svg, "data:image/png;base64,cG5n"));
    CHECK(contains(svg, "</svg>"));
}

void test_radial_gradient_and_opacity() {
    ArtDocument doc;
    doc.width = 10.0;
    doc.height = 10.0;
    ArtElement e;
    e.node.segments = {move(0, 0), line(5, 0), line(5, 5), close()};
    e.node.opacity = 0.25;
    e.node.paint.hasFill = true;
    e.node.paint.hasGradient = true;
    e.node.paint.gradient.radial = true;
    e.node.paint.gradient.stops = {{0.0f, {1, 2, 3, 255}}};
    doc.elements.push_back(e);
    const std::string svg = artDocumentToSvg(doc);
    CHECK(contains(svg, "<radialGradient"));
    CHECK(contains(svg, "cx=\"0.5\" cy=\"0.5\" r=\"0.5\""));
    CHECK(contains(svg, "opacity=\"0.25\""));
}

void test_empty_document_is_empty() {
    CHECK_EQ(artDocumentToSvg(ArtDocument{}).size(), 0u);
    ArtDocument d;
    d.width = 10.0;
    d.height = 0.0;
    CHECK(artDocumentToSvg(d).empty());
}

void test_custom_viewbox() {
    ArtDocument doc;
    doc.width = 100.0;
    doc.height = 50.0;
    doc.viewX = 10.0;
    doc.viewY = 20.0;
    doc.viewW = 40.0;
    doc.viewH = 20.0;
    const std::string svg = artDocumentToSvg(doc);
    CHECK(contains(svg, "viewBox=\"10 20 40 20\""));
}

// A decoder-rebuilt shape becomes a real ArtNode: anchors turn into cubics,
// the paint maps across, and the gradient resolves to the rasteriser's maths.
void test_art_from_shape() {
    VectorShape shape;
    SubPath sub;
    sub.anchors = {Anchor::corner(0, 0), Anchor::corner(10, 0),
                   Anchor::corner(10, 10), Anchor::corner(0, 10)};
    sub.closed = true;
    shape.path.subpaths.push_back(sub);
    shape.path.name = "square";
    shape.fill = {0x10, 0x20, 0x30, 0xff};
    shape.hasStroke = true;
    shape.stroke = {0x01, 0x02, 0x03, 0x80};
    shape.strokeWidth = 2.5f;
    shape.evenOdd = true;

    auto node = artNodeFromShape(shape, nullptr, -2.0, -3.0);
    CHECK(node != nullptr);
    if (!node) return;
    CHECK(node->name == "square");
    // MoveTo + one cubic per anchor + Close.
    CHECK_EQ(node->segments.size(), 6u);
    CHECK(node->segments[0].kind == Segment::Kind::MoveTo);
    CHECK_EQ(static_cast<double>(node->segments[0].x), 0.0);
    CHECK(node->segments[1].kind == Segment::Kind::CubicTo);
    CHECK(node->segments.back().kind == Segment::Kind::Close);
    CHECK(node->evenOdd);
    CHECK(node->paint.hasFill);
    CHECK(node->paint.hasStroke);
    CHECK_EQ(static_cast<int>(node->paint.fill[2]), 0x30);
    CHECK_EQ(static_cast<int>(node->paint.stroke[3]), 0x80);
    CHECK_NEAR(node->paint.strokeWidth, 2.5, 1e-9);
    CHECK_EQ(node->paint.cap, 1);   // round, matching the rasteriser
    CHECK_EQ(node->paint.join, 1);
    CHECK_NEAR(node->matrix[4], -2.0, 1e-12);
    CHECK_NEAR(node->matrix[5], -3.0, 1e-12);

    // Linear gradient: endpoints map straight through.
    GradientFill lin;
    lin.stops = {{0.0f, {1, 2, 3, 255}}, {1.0f, {4, 5, 6, 128}}};
    lin.startX = 1.0;
    lin.startY = 2.0;
    lin.endX = 5.0;
    lin.endY = 6.0;
    auto g = artNodeFromShape(shape, &lin, 0.0, 0.0);
    CHECK(g != nullptr);
    if (g) {
        CHECK(g->paint.hasGradient);
        CHECK(!g->paint.gradient.radial);
        CHECK_NEAR(g->paint.gradient.x1, 1.0, 1e-12);
        CHECK_NEAR(g->paint.gradient.y2, 6.0, 1e-12);
        CHECK_EQ(g->paint.gradient.stops.size(), 2u);
        CHECK_EQ(static_cast<int>(g->paint.gradient.stops[1].rgba[3]), 128);
    }

    // Radial gradient: centre is `start`, radius is |end - start|.
    GradientFill rad;
    rad.radial = true;
    rad.stops = {{0.0f, {9, 9, 9, 255}}};
    rad.startX = 3.0;
    rad.startY = 4.0;
    rad.endX = 3.0;
    rad.endY = 9.0;
    auto r = artNodeFromShape(shape, &rad, 0.0, 0.0);
    CHECK(r != nullptr);
    if (r) {
        CHECK(r->paint.gradient.radial);
        CHECK_NEAR(r->paint.gradient.cx, 3.0, 1e-12);
        CHECK_NEAR(r->paint.gradient.cy, 4.0, 1e-12);
        CHECK_NEAR(r->paint.gradient.r, 5.0, 1e-12);
    }

    // A gradient stands in for a flat fill even when the fill byte is clear.
    VectorShape bare;
    bare.path.subpaths = {sub};
    auto bareNode = artNodeFromShape(bare, &lin, 0.0, 0.0);
    CHECK(bareNode != nullptr);
    if (bareNode) CHECK(bareNode->paint.hasFill);

    // No subpaths: nothing retained.
    VectorShape empty;
    CHECK(artNodeFromShape(empty, nullptr, 0.0, 0.0) == nullptr);
}

}  // namespace

int main() {
    test_base64_vectors();
    test_art_node_round_trip();
    test_truncated_blob_is_rejected();
    test_document_writes_real_vector();
    test_radial_gradient_and_opacity();
    test_empty_document_is_empty();
    test_custom_viewbox();
    test_art_from_shape();
    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}
