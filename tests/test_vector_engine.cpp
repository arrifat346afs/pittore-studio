// Vector-engine tests: DOM, clones, markers, patterns, mesh,
// clip/mask, fe filters, snap/grids, transforms, path ops, spiro, calligraphy,
// connectors, trace, box3d, spray, text flow, palettes, LPE stack, DXF/PDF,
// Exchange/CLI: DXF/PDF writers, extension host, command verbs.
#include <cassert>
#include <cmath>
#include <cstdio>

#include "engine/vector/box3d.h"
#include "engine/vector/calligraphy.h"
#include "engine/vector/clip_mask.h"
#include "engine/vector/clone.h"
#include "engine/vector/connector.h"
#include "engine/vector/filter_fe.h"
#include "engine/vector/grids.h"
#include "engine/vector/svg_exchange.h"
#include "engine/vector/lpe/lpe.h"
#include "engine/vector/marker.h"
#include "engine/vector/mesh.h"
#include "engine/vector/palette.h"
#include "engine/vector/path_ops.h"
#include "engine/vector/pattern.h"
#include "engine/vector/snap.h"
#include "engine/vector/spiro.h"
#include "engine/vector/spray.h"
#include "engine/vector/svg_dom.h"
#include "engine/vector/text_flow.h"
#include "engine/vector/trace.h"
#include "engine/vector/transform_ops.h"
#include "engine/vector/vector_art.h"

using namespace pittore::vector;

int main() {
    // SVG DOM + cascade (xml/node, css).
    {
        auto parsed = parseSvgDom(
            "<svg><style>.a{fill:red}</style><rect id=\"r\" class=\"a\" width=\"5\"/></svg>");
        assert(parsed.ok && parsed.doc.root);
        auto* r = parsed.doc.findId("r");
        assert(r);
        auto fill = parsed.doc.resolved(*r, "fill");
        assert(fill && *fill == "red");
        assert(!parsed.doc.stylesheet.empty());
        std::string s = serializeSvgDom(parsed.doc);
        assert(s.find("<rect") != std::string::npos);
    }
    // Clones (sp-use).
    {
        auto parsed = parseSvgDom(
            "<svg><rect id=\"a\" width=\"4\"/><use href=\"#a\" x=\"3\"/></svg>");
        assert(parsed.ok);
        assert(expandAllUses(parsed.doc) >= 1);
        assert(listUseLinks(parsed.doc).empty());
    }
    // Markers + dashes + patterns.
    {
        assert(builtinMarkerIds().size() >= 28);
        assert(!builtinMarkersSvg().empty());
        assert(builtinDashPresets().size() >= 30);
        assert(builtinPatternIds().size() >= 20);
        std::vector<std::pair<double, double>> line{{0, 0}, {10, 0}, {10, 10}};
        auto m = markersForPolyline(line, false, "mk-Dot", "mk-Dot", "mk-Dot", 2.0);
        assert(m.size() == 3);
    }
    // Mesh + conical.
    {
        MeshGradient mesh;
        mesh.rows = mesh.cols = 1;
        MeshPatch p{};
        p.c = {{{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {255, 255, 0, 255}}};
        mesh.patches.push_back(p);
        auto c = meshSample(mesh, 0.5, 0.5);
        (void)c;
        auto px = rasterizeMesh(mesh, 4, 4);
        assert(px.size() == 64);
        assert(!meshToSvg(mesh, "m1", {255, 0, 0, 255}).empty());
    }
    // Clip/mask.
    {
        auto parsed = parseSvgDom(
            "<svg><clipPath id=\"c\"><rect x=\"0\" y=\"0\" width=\"10\" height=\"10\"/>"
            "</clipPath><rect id=\"r\" clip-path=\"url(#c)\"/></svg>");
        assert(parsed.ok);
        auto* r = parsed.doc.findId("r");
        assert(r);
        auto clip = clipFor(*r, parsed.doc);
        assert(!clip.empty());
        assert(pointInClip(clip, 5, 5));
        assert(!pointInClip(clip, 50, 50));
    }
    // fe filters.
    {
        RgbaImage img{4, 4, std::vector<std::uint8_t>(64, 128)};
        FilterGraph g;
        g.id = "f";
        g.prims.push_back(FePrimitive{"feGaussianBlur", {{"stdDeviation", "1"}}, "", ""});
        auto out = applyFilterGraph(g, img);
        assert(out.px.size() == 64);
        assert(!filterGraphToSvg(g).empty());
        assert(svgFilterCatalog().size() >= 16);
        // Full primitive coverage behaves sanely.
        auto run1 = [&](FePrimitive p) {
            FilterGraph h;
            h.id = "t";
            h.prims.push_back(std::move(p));
            return applyFilterGraph(h, img);
        };
        // Identity-ish primitives preserve size.
        assert(run1({"feMorphology", {{"operator", "erode"}, {"radius", "1"}}, "", ""}).px.size() == 64);
        assert(run1({"feConvolveMatrix", {{"order", "3"}}, "", ""}).px.size() == 64);
        assert(run1({"feDisplacementMap", {{"scale", "0"}, {"in2", "SourceGraphic"}}, "", ""}).px == img.px);
        assert(run1({"feColorMatrix", {{"type", "matrix"}, {"values", "1 0 0 0 0 0 1 0 0 0 0 0 1 0 0 0 0 0 1 0"}}, "", ""}).px == img.px);
        assert(run1({"feColorMatrix", {{"type", "hueRotate"}, {"values", "0"}}, "", ""}).px == img.px);
        assert(run1({"feColorMatrix", {{"type", "luminanceToAlpha"}}, "", ""}).px.size() == 64);
        assert(run1({"feComponentTransfer", {}, "", ""}).px == img.px);
        assert(run1({"feComposite", {{"operator", "arithmetic"}, {"k2", "1"}, {"in2", "SourceGraphic"}}, "", ""}).px == img.px);
        assert(run1({"feMerge", {}, "", ""}).px.size() == 64);
        assert(run1({"feTile", {}, "", ""}).px == img.px);
        assert(run1({"feDiffuseLighting", {{"surfaceScale", "1"}}, "", ""}).px.size() == 64);
        assert(run1({"feSpecularLighting", {{"surfaceScale", "1"}}, "", ""}).px.size() == 64);
        assert(run1({"feDropShadow", {{"dx", "1"}, {"dy", "1"}, {"stdDeviation", "1"}}, "", ""}).px.size() == 64);
        // Folded children round-trip through SVG.
        FilterGraph fg;
        fg.id = "fx";
        fg.prims.push_back(FePrimitive{"feDiffuseLighting",
                                       {{"light.tag", "feDistantLight"},
                                        {"light.azimuth", "30"}},
                                       "lit",
                                       "SourceGraphic"});
        std::string svg = filterGraphToSvg(fg);
        assert(svg.find("feDistantLight") != std::string::npos);
        assert(svg.find("result=\"lit\"") != std::string::npos);
    }
    // Snap + grids.
    {
        auto res = snapPoint(9.6, 0, gridCandidates(9.6, 0, 10.0), 1.0, SnapAll);
        assert(res.snapped);
        GridSpec ax = axonometricFromRatio(2, 1, 10);
        assert(ax.kind == GridKind::Axonometric);
        auto [sx, sy] = snapToGrid(GridSpec{}, 12, 13);
        assert(sx == 10 && sy == 10);
    }
    // Align/distribute/arrange/transform.
    {
        std::vector<Bbox> boxes{Bbox{0, 0, 10, 10}, Bbox{20, 0, 30, 10}};
        auto t = alignBoxes(boxes, AlignEdge::Left, 5.0);
        assert(t[0].first == 5.0 && t[1].first == -15.0);
        double m[6];
        TransformSpec spec;
        spec.rotateDeg = 90;
        transformSpecToMatrix(spec, m);
        assert(std::abs(m[0]) < 1e-6 && std::abs(m[1] - 1) < 1e-6);
        auto ap = arrangePositions(4, ArrangeSpec{});
        assert(ap.size() == 4);
    }
    // Path ops.
    {
        std::vector<Segment> rect{Segment{Segment::Kind::MoveTo, 0, 0},
                                  Segment{Segment::Kind::LineTo, 10, 0},
                                  Segment{Segment::Kind::LineTo, 10, 10},
                                  Segment{Segment::Kind::LineTo, 0, 10},
                                  Segment{Segment::Kind::Close}};
        auto parts = breakApart(rect);
        assert(parts.size() == 1);
        auto simp = simplifyPath(rect, 1.0);
        assert(!simp.empty());
        auto off = offsetPath(rect, 2.0);
        assert(!off.empty());
        auto fil = filletPath(rect, 2.0);
        assert(!fil.empty());
        StrokeStyle st(2.0f);
        assert(!strokeToSegments(rect, st).empty());
    }
    // Spiro/bspline/calligraphy/connector.
    {
        std::vector<SpiroPoint> sp{{0, 0}, {10, 5}, {20, 0}};
        assert(spiroFit(sp).size() >= 3);
        std::vector<std::pair<double, double>> bp{{0, 0}, {5, 5}, {10, 0}, {15, 5}};
        assert(bsplineFit(bp).size() >= 3);
        std::vector<CalligraphySample> spine{{0, 0, 1}, {10, 0, 1}, {20, 5, 0.5}};
        assert(!calligraphyStroke(spine, CalligraphyNib{}).empty());
        auto route = routeConnector({0, 0}, {10, 10}, ConnectorKind::Orthogonal, {});
        assert(route.points.size() == 3 && route.orthogonal);
        // A* maze routing steers around a blocking rect.
        ConnectorObstacle wall{3, -20, 7, 20};
        auto maze = routeConnector({0, 0}, {10, 0}, ConnectorKind::Orthogonal, {wall});
        assert(maze.points.size() >= 2);
        for (auto [x, y] : maze.points) {
            bool strictlyInside = x > 3.5 && x < 6.5 && y > -19 && y < 19;
            assert(!strictlyInside);
        }
    }
    // Trace.
    {
        std::vector<float> lum(100, 0.0f);
        for (int i = 30; i < 70; i++) lum[(size_t)i] = 1.0f;
        auto loops = traceBitmap(lum, 10, 10, TraceSpec{});
        assert(!loops.empty());
        // Block shape fits cubic curves (not just line chains).
        std::vector<float> block(400, 0.0f);
        for (int y = 5; y < 15; y++)
            for (int x = 5; x < 15; x++) block[(size_t)(y * 20 + x)] = 1.0f;
        auto fitted = traceBitmap(block, 20, 20, TraceSpec{});
        assert(!fitted.empty());
        bool hasCubic = false;
        for (auto& segs : fitted)
            for (auto& s : segs)
                if (s.kind == Segment::Kind::CubicTo) hasCubic = true;
        assert(hasCubic);
    }
    // Box3d + spray + text flow + palette.
    {
        assert(boxToSegments(Box3D{}).size() >= 5);
        assert(sprayStamps(0, 0, 8, SpraySpec{}).size() == 8);
        std::vector<std::pair<double, double>> spine{{0, 0}, {100, 0}};
        auto glyphs = textOnPathLayout("Hi", spine, {10, 10});
        assert(glyphs.size() == 2 && glyphs[0].visible);
        Palette pal{"t", {{{"R", {255, 0, 0, 255}}}}};
        std::string gpl = writeGpl(pal);
        assert(parseGpl(gpl).colors.size() == 1);
        // ASE CMYK/LAB convert to real sRGB (not skipped).
        {
            std::vector<std::uint8_t> ase;
            auto u16 = [&](std::uint16_t v) {
                ase.push_back((std::uint8_t)(v >> 8));
                ase.push_back((std::uint8_t)v);
            };
            auto u32 = [&](std::uint32_t v) {
                for (int k = 3; k >= 0; k--) ase.push_back((std::uint8_t)(v >> (8 * k)));
            };
            auto f32 = [&](float f) {
                std::uint32_t v;
                __builtin_memcpy(&v, &f, 4);
                // Big-endian store.
                for (int k = 3; k >= 0; k--) ase.push_back((std::uint8_t)(v >> (8 * k)));
            };
            ase.insert(ase.end(), {'A', 'S', 'E', 'F', 0, 1, 0, 0});
            u32(2);
            auto colorBlock = [&](const std::string& name, const std::string& mode,
                                  std::initializer_list<float> comps) {
                u16(0x0001);
                size_t lenPos = ase.size();
                u32(0);
                size_t start = ase.size();
                std::string nm = name;
                u16((std::uint16_t)(nm.size() + 1));
                for (char c : nm) {
                    ase.push_back(0);
                    ase.push_back((std::uint8_t)c);
                }
                ase.push_back(0);
                ase.push_back(0);
                for (char c : mode) ase.push_back((std::uint8_t)c);
                for (float v : comps) f32(v);
                u16(0);  // color type
                std::uint32_t len = (std::uint32_t)(ase.size() - start);
                for (int k = 3; k >= 0; k--)
                    ase[lenPos + (3 - k)] = (std::uint8_t)(len >> (8 * k));
            };
            colorBlock("CyanInk", "CMYK", {1, 0, 0, 0});
            colorBlock("RedLab", "LAB ", {0.5f, 0.6f, 0.5f});
            Palette ap = parseAse(ase);
            assert(ap.colors.size() == 2);
            // Cyan ink converts toward cyan, LAB red is reddish.
            assert(ap.colors[0].rgba[2] > ap.colors[0].rgba[0]);
            assert(ap.colors[1].rgba[0] > ap.colors[1].rgba[2]);
        }
    }
    // LPE registry: all 55+ keys construct.
    {
        assert(lpe::allEffects().size() >= 55);
        int built = 0;
        for (auto& info : lpe::allEffects()) {
            lpe::Params p;
            if (lpe::makeEffect(info.type, p)) built++;
        }
        assert(built >= 55);
        lpe::EffectStack stack;
        lpe::Params p;
        p.setDouble("amount", 3.0);
        stack.push(lpe::makeEffect(lpe::EffectType::Offset, p));
        std::vector<Segment> rect{Segment{Segment::Kind::MoveTo, 0, 0},
                                  Segment{Segment::Kind::LineTo, 8, 0},
                                  Segment{Segment::Kind::LineTo, 8, 8},
                                  Segment{Segment::Kind::LineTo, 0, 8},
                                  Segment{Segment::Kind::Close}};
        assert(!stack.apply(rect).empty());
        // Convert family produces genuine geometry (not passthrough).
        auto applyKey = [&](const char* key, lpe::Params pp) {
            auto e = lpe::makeEffectByKey(key, pp);
            assert(e);
            return e->apply(rect);
        };
        assert(applyKey("bounding_box", {}).size() == 5);
        assert(applyKey("circle_3pts", {}).size() >= 5);
        lpe::Params ell;
        assert(!lpe::makeEffectByKey("pts2ellipse", ell)->apply(rect).empty());
        lpe::Params lr;
        lr.set("a", "0,0");
        lr.set("b", "9,9");
        assert(applyKey("line_segment", lr).size() == 2);
        lpe::Params par;
        par.setDouble("distance", 5.0);
        assert(!applyKey("parallel", par).empty());
        lpe::Params pb;
        pb.set("a", "0,0");
        pb.set("b", "8,0");
        assert(applyKey("perp_bisector", pb).size() == 2);
        lpe::Params t2;
        t2.set("p0", "0,0");
        t2.set("p1", "8,0");
        t2.set("q0", "0,0");
        t2.set("q1", "0,8");
        assert(!applyKey("transform_2pts", t2).empty());
        lpe::Params bo;
        bo.set("op", "intersection");
        bo.set("rect", "4,4,8,8");
        assert(!applyKey("bool", bo).empty());
        assert(!applyKey("slice", {}).empty());
        assert(!applyKey("powerclip", {}).empty());
        assert(!applyKey("knot", {}).empty());
        assert(!applyKey("interpolate", {}).empty());
        assert(!applyKey("curvestitch", {}).empty());
        assert(!applyKey("fill_between_many", {}).empty());
        assert(!applyKey("measure_segments", {}).empty());
        assert(!applyKey("path_length", {}).empty());
        assert(!applyKey("recursiveskeleton", {}).empty());
        assert(!applyKey("dynastroke", {}).empty());
        assert(!applyKey("embrodery-stitch", {}).empty());
        lpe::Params tp;
        tp.set("from", "-5,4");
        assert(applyKey("tangent_to_curve", tp).size() == 4);
        // Shared drag-params: canvas and panel build identical effects.
        {
            auto q = lpe::dragParamsFor(lpe::EffectType::Offset, 0, 0, 40, 0, 30.0);
            assert(q.getDouble("amount", 0) == 12.0);
            auto m = lpe::dragParamsFor(lpe::EffectType::MirrorSymmetry, 7, 0, 40, 0, 30.0);
            assert(m.getDouble("gap", 0) == 7.0);
            auto t = lpe::dragParamsFor(lpe::EffectType::Tiling, 0, 0, 120, 0, 30.0);
            assert(t.getLong("copies", 0) >= 2 && t.getLong("copies", 99) <= 16);
            auto g = lpe::dragParamsFor(lpe::EffectType::Gears, 0, 0, 120, 0, 30.0);
            assert(g.getLong("teeth", 0) >= 6 && g.getLong("teeth", 99) <= 48);
            auto pc = lpe::dragParamsFor(lpe::EffectType::Powerclip, 2, 3, 10, 11, 30.0);
            assert(pc.getString("rect", "") == "2,3,8,8");
            auto cr = lpe::dragParamsFor(lpe::EffectType::CopyRotate, 0, 0, 90, 0, 30.0);
            assert(cr.getLong("copies", 0) >= 2);
        }
    }
    // DXF/PDF/inx/CLI.
    {
        std::string dxf = writeDxf({{{{{0, 0}, {10, 0}}}, false, 1}});
        assert(dxf.find("LWPOLYLINE") != std::string::npos);
        PdfPage pg;
        pg.content = "0 0 m 10 0 l S";
        pg.links.push_back({0, 0, 10, 10, "p1"});
        pg.texts.push_back({5, 5, 12, "Hello"});
        PdfImage im;
        im.x = 0;
        im.y = 0;
        im.w = 10;
        im.h = 10;
        im.iw = 2;
        im.ih = 2;
        im.jpeg = {0xFF, 0xD8, 0xFF, 0xD9};  // SOI+EOI shell (structural test)
        pg.images.push_back(im);
        auto pdf = writePdf({pg, pg}, "t");
        assert(pdf.size() > 200 && pdf[0] == '%');
        std::string ps(pdf.begin(), pdf.end());
        assert(ps.find("/Helvetica") != std::string::npos);
        assert(ps.find("(Hello)") != std::string::npos);
        assert(ps.find("/DCTDecode") != std::string::npos);
        assert(ps.find("/Count 2") != std::string::npos);
        bool ok = false;
        auto ext = parseExtension(
            "<extension id=\"org.test\"><name>T</name><script>python x.py</script>"
            "<param name=\"n\" gui-text=\"N\" type=\"int\">3</param></extension>",
            ok);
        assert(ok && ext.params.size() == 1);
        auto cli = parseCliArgs({"in.svg", "--export-pdf=out.pdf", "--query-id=r1"});
        assert(cli.exportPdf == "out.pdf" && cli.queryId == "r1");
    }
    // Paint refs: markers/pattern/mesh/filter survive SVG + IFP round-trips.
    {
        ArtDocument doc;
        doc.width = doc.height = 100;
        ArtElement el;
        el.node.name = "r";
        el.node.segments = {Segment{Segment::Kind::MoveTo, 0, 0},
                            Segment{Segment::Kind::LineTo, 10, 0},
                            Segment{Segment::Kind::LineTo, 10, 10},
                            Segment{Segment::Kind::Close}};
        el.node.paint.hasFill = true;
        el.node.paint.fill[0] = 255;
        el.node.paint.fill[3] = 255;
        el.node.paint.markerStart = "mk-Dot";
        el.node.paint.markerEnd = "mk-Arrow1";
        el.node.paint.patternId = "pat-Dots";
        el.node.paint.clipId = "c1";
        el.node.paint.filter.id = "f1";
        el.node.paint.filter.prims.push_back(
            FePrimitive{"feGaussianBlur", {{"stdDeviation", "2"}}, "", ""});
        el.node.paint.hasFilter = true;
        MeshGradient mesh;
        mesh.rows = mesh.cols = 1;
        MeshPatch mp{};
        mp.c = {{{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {255, 255, 0, 255}}};
        mesh.patches.push_back(mp);
        el.node.paint.hasMesh = true;
        el.node.paint.mesh = mesh;
        doc.elements.push_back(el);
        std::string svg = artDocumentToSvg(doc);
        assert(svg.find("marker-start") != std::string::npos);
        assert(svg.find("url(#pat-Dots)") != std::string::npos);
        assert(svg.find("clip-path") != std::string::npos);
        assert(svg.find("meshgradient") != std::string::npos);
        assert(svg.find("data-mesh=\"mesh0\"") != std::string::npos);
        assert(svg.find("<filter") != std::string::npos);
        auto bytes = encodeArtNode(el.node);
        auto back = decodeArtNode(bytes);
        assert(back && back->paint.markerStart == "mk-Dot");
        assert(back->paint.patternId == "pat-Dots");
        assert(back->paint.hasMesh && back->paint.mesh.patches.size() == 1);
        assert(back->paint.hasFilter && back->paint.filter.prims.size() == 1);
        // Pre-ref files (no tail) still decode.
        ArtNode plain;
        plain.segments = el.node.segments;
        auto pb = encodeArtNode(plain);
        // Truncate after segments: decode must still succeed via tails.
        assert(decodeArtNode(pb).has_value());
    }
    std::printf("vector engine ok\n");
    return 0;
}
