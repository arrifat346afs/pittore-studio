// test_af_shapes.cpp — end-to-end check of the live-shape / free-path decode:
// read each shape probe, composite the recovered layers over white, and compare
// against the file's own embedded document thumbnail (Affinity's render of the
// same document). RMS bounds match the tolerances the importer targets.
#include <cstdio>

#include "af_probe.h"
#include "test_util.h"

namespace {

using pittore::probe::decodeThumb;
using pittore::probe::probeDir;
using pittore::probe::readFile;
using pittore::probe::thumbRms;

struct Bound {
    const char* file;
    double rms;
};

const Bound kBounds[] = {
    {"shp_arrow.af", 1.0},           {"shp_callout_ellipse.af", 1.0},
    {"shp_callout_rrect.af", 1.0},   {"shp_cog.af", 2.0},
    {"shp_corner_concave.af", 1.0},  {"shp_corner_cutout.af", 0.5},
    {"shp_corner_straight.af", 0.5}, {"shp_crescent.af", 1.0},
    {"shp_diamond.af", 0.5},         {"shp_donut.af", 1.0},
    {"shp_doublestar.af", 0.5},      {"shp_pie.af", 1.0},
    {"shp_polygon.af", 0.5},         {"shp_segment.af", 1.0},
    {"shp_star_curved.af", 4.5},     {"shp_tear.af", 2.5},
    {"shp_trapezoid.af", 0.5},       {"shp_triangle.af", 0.5},
};

// Document-space control-point bounds of a retained shape node. `Close`
// segments carry no point.
bool artBounds(const pittore::vector::ArtNode& n, double ox, double oy,
               double* x0, double* y0, double* x1, double* y1) {
    const double* m = n.matrix;
    bool any = false;
    for (const auto& s : n.segments) {
        if (s.kind == pittore::vector::Segment::Kind::Close) continue;
        const double xs[3] = {s.x, s.c1x, s.c2x};
        const double ys[3] = {s.y, s.c1y, s.c2y};
        const int count = s.kind == pittore::vector::Segment::Kind::CubicTo ? 3 : 1;
        for (int i = 0; i < count; ++i) {
            const double px = m[0] * xs[i] + m[2] * ys[i] + m[4] + ox;
            const double py = m[1] * xs[i] + m[3] * ys[i] + m[5] + oy;
            if (!any) {
                *x0 = *x1 = px;
                *y0 = *y1 = py;
                any = true;
            } else {
                *x0 = std::min(*x0, px);
                *y0 = std::min(*y0, py);
                *x1 = std::max(*x1, px);
                *y1 = std::max(*y1, py);
            }
        }
    }
    return any;
}

void run() {
    const std::string dir = probeDir();
    if (dir.empty()) return;
    int tested = 0;
    int kept = 0;
    for (const Bound& b : kBounds) {
        auto bytes = readFile(dir + "/" + b.file);
        if (!bytes) continue;
        auto doc = pittore::io::afDecodeLayers(*bytes, nullptr, dir);
        CHECK(doc.has_value());
        if (!doc) continue;
        auto thumb = decodeThumb(*bytes);
        CHECK(thumb.has_value());
        if (!thumb) continue;

        const double rms = thumbRms(*doc, *thumb);
        std::printf("  %-26s %ux%u rms %.2f (bound %.1f)\n", b.file, doc->width, doc->height,
                    rms, b.rms);
        CHECK(rms <= b.rms);
        ++tested;

        // Every drawable shape also keeps its true geometry, and that geometry
        // sits inside the raster footprint (which is the path padded out).
        int artLayers = 0;
        for (const pittore::io::AfLayer& l : doc->layers) {
            if (l.isGroup || !l.art || l.art->segments.empty()) continue;
            ++artLayers;
            double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
            CHECK(artBounds(*l.art, l.left, l.top, &x0, &y0, &x1, &y1));
            const double tol = 3.0;
            CHECK(x0 >= l.left - tol);
            CHECK(y0 >= l.top - tol);
            CHECK(x1 <= l.left + static_cast<double>(l.width) + tol);
            CHECK(y1 <= l.top + static_cast<double>(l.height) + tol);
            // A shape's fill colour is part of the retained paint too.
            const bool painted = l.art->paint.hasFill || l.art->paint.hasStroke ||
                                 l.art->paint.hasGradient;
            CHECK(painted);
        }
        CHECK(artLayers >= 1);
        if (artLayers >= 1) ++kept;

        // The retained geometry serializes as real vector paths: no bitmap
        // fallback for a pure-shape probe.
        pittore::vector::ArtDocument artDoc;
        artDoc.width = doc->width;
        artDoc.height = doc->height;
        for (auto it = doc->layers.rbegin(); it != doc->layers.rend(); ++it) {
            if (it->isGroup || !it->art || it->art->segments.empty()) continue;
            pittore::vector::ArtElement el;
            el.node = *it->art;
            // Fold the layer's placement in, as the exporter does.
            el.node.matrix[4] += static_cast<double>(it->left);
            el.node.matrix[5] += static_cast<double>(it->top);
            artDoc.elements.push_back(std::move(el));
        }
        if (!artDoc.elements.empty()) {
            const std::string svg = pittore::vector::artDocumentToSvg(artDoc);
            CHECK(svg.find("<path ") != std::string::npos);
            CHECK(svg.find("<image ") == std::string::npos);
        }
    }
    std::printf("  shape probes tested: %d, retained vector: %d\n", tested, kept);
}

}  // namespace

TEST_MAIN_CALL(run)
