// Large-SVG probes: San Francisco (80k paths) and Brazil (5k paths).
// Test-first gate for the Inkscape-style speed work. Nothing here touches
// the engine: fast scanners and path bounds below are clean-room prototypes
// written from the SVG2 grammar. They run against the two large fixtures and
// report baseline vs prototype timings. Perf never fails, only correctness
// does. Skips cleanly when the fixtures are absent (never commit them).
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "engine/vector/svg/bounds.h"
#include "engine/vector/svg/css_parse.h"
#include "engine/vector/svg/path_data.h"
#include "engine/vector/svg/render_item.h"
#include "engine/vector/svg/scene.h"
#include "engine/vector/svg/xml_reader.h"
#include "test_util.h"

using namespace pittore::svg;

namespace {

// ---- fixture lookup (no machine paths baked in) ---------------------------

std::string svgDir() {
    if (const char* e = std::getenv("PITTORE_SVG_DIR")) {
        return std::string(e);
    }
#ifdef PITTORE_SOURCE_ROOT
    return std::string(PITTORE_SOURCE_ROOT) + "/tests/SVG";
#else
    return std::string("tests/SVG");
#endif
}

bool loadFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return false;
    }
    std::ostringstream s;
    s << f.rdbuf();
    out = s.str();
    return !out.empty();
}

double nowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch())
        .count();
}

size_t countNeedle(const std::string& hay, const char* needle) {
    size_t n = 0;
    size_t pos = 0;
    const size_t len = std::char_traits<char>::length(needle);
    while ((pos = hay.find(needle, pos)) != std::string::npos) {
        ++n;
        pos += len;
    }
    return n;
}

// ---- clean-room prototypes (test-only, from SVG2 grammar) -----------------

bool fastNum(const char*& p, const char* end, double& out) {
    const char* s = p;
    bool neg = false;
    if (s < end && (*s == '+' || *s == '-')) {
        neg = *s == '-';
        ++s;
    }
    bool any = false;
    double ip = 0;
    while (s < end && *s >= '0' && *s <= '9') {
        ip = ip * 10 + (*s - '0');
        ++s;
        any = true;
    }
    double fr = 0, sc = 1;
    if (s < end && *s == '.') {
        ++s;
        while (s < end && *s >= '0' && *s <= '9') {
            fr = fr * 10 + (*s - '0');
            sc *= 10;
            ++s;
            any = true;
        }
    }
    if (!any) {
        return false;
    }
    double v = ip + fr / sc;
    if (s < end && (*s == 'e' || *s == 'E')) {
        const char* e0 = s++;
        bool eneg = false;
        if (s < end && (*s == '+' || *s == '-')) {
            eneg = *s == '-';
            ++s;
        }
        int ex = 0;
        bool ed = false;
        while (s < end && *s >= '0' && *s <= '9') {
            ex = ex * 10 + (*s - '0');
            ++s;
            ed = true;
        }
        if (ed) {
            double pw = 1;
            for (int i = 0; i < ex; ++i) {
                pw *= 10;
            }
            v = eneg ? v / pw : v * pw;
        } else {
            s = e0;
        }
    }
    out = neg ? -v : v;
    p = s;
    return true;
}

void fastDoubles(std::string_view in, std::vector<double>& out) {
    const char* p = in.data();
    const char* end = p + in.size();
    for (;;) {
        while (p < end &&
               (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' ||
                *p == ',')) {
            ++p;
        }
        if (p >= end) {
            return;
        }
        double v = 0;
        if (!fastNum(p, end, v)) {
            return;
        }
        out.push_back(v);
    }
}

bool protoCmd(char c) {
    return c == 'M' || c == 'm' || c == 'L' || c == 'l' || c == 'H' ||
           c == 'h' || c == 'V' || c == 'v' || c == 'C' || c == 'c' ||
           c == 'S' || c == 's' || c == 'Q' || c == 'q' || c == 'T' ||
           c == 't' || c == 'A' || c == 'a' || c == 'Z' || c == 'z';
}

int protoNeed(char c) {
    switch (c) {
        case 'M':
        case 'm':
        case 'L':
        case 'l':
        case 'T':
        case 't':
            return 2;
        case 'H':
        case 'h':
        case 'V':
        case 'v':
            return 1;
        case 'C':
        case 'c':
            return 6;
        case 'S':
        case 's':
        case 'Q':
        case 'q':
            return 4;
        case 'A':
        case 'a':
            return 7;
        default:
            return 0;
    }
}

SegType protoType(char c) {
    switch (c) {
        case 'M':
        case 'm':
            return SegType::Move;
        case 'L':
        case 'l':
            return SegType::Line;
        case 'H':
        case 'h':
            return SegType::H;
        case 'V':
        case 'v':
            return SegType::V;
        case 'C':
        case 'c':
            return SegType::Cubic;
        case 'S':
        case 's':
            return SegType::SmoothCubic;
        case 'Q':
        case 'q':
            return SegType::Quad;
        case 'T':
        case 't':
            return SegType::SmoothQuad;
        case 'A':
        case 'a':
            return SegType::Arc;
        default:
            return SegType::Close;
    }
}

// View-based path scan. No string copy, no strtod, bounded: every failed
// param attempt consumes at least one char, so bad input cannot hang.
void fastPath(std::string_view in, std::vector<PathSeg>& out) {
    const char* p = in.data();
    const char* end = p + in.size();
    auto skip = [&] {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' ||
                           *p == '\r' || *p == ',')) {
            ++p;
        }
    };
    char cur = 0;
    bool first = true;
    skip();
    while (p < end) {
        if (protoCmd(*p)) {
            cur = *p++;
            first = true;
            if (cur == 'Z' || cur == 'z') {
                PathSeg g;
                g.type = SegType::Close;
                g.rel = false;
                g.n = 0;
                out.push_back(g);
                cur = 0;
            }
            skip();
            continue;
        }
        if (cur == 0) {
            ++p;
            skip();
            continue;
        }
        const int want = protoNeed(cur);
        if (want == 0) {
            cur = 0;
            continue;
        }
        const char* save = p;
        double vv[7] = {0, 0, 0, 0, 0, 0, 0};
        bool good = true;
        for (int k = 0; k < want; ++k) {
            skip();
            if (!fastNum(p, end, vv[k])) {
                good = false;
                break;
            }
        }
        if (!good) {
            p = save;
            skip();
            if (p < end && protoCmd(*p)) {
                continue;
            }
            if (p < end) {
                ++p;
            }
            skip();
            continue;
        }
        char use = cur;
        if ((cur == 'M' || cur == 'm') && !first) {
            use = (cur == 'M') ? 'L' : 'l';
        }
        PathSeg g;
        g.type = protoType(use);
        g.rel = use >= 'a' && use <= 'z';
        for (int k = 0; k < want; ++k) {
            g.v[k] = vv[k];
        }
        g.n = want;
        out.push_back(g);
        first = false;
        skip();
    }
}

struct Box {
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool empty = true;
    void add(double x, double y) {
        if (empty) {
            x0 = x1 = x;
            y0 = y1 = y;
            empty = false;
            return;
        }
        if (x < x0) x0 = x;
        if (y < y0) y0 = y;
        if (x > x1) x1 = x;
        if (y > y1) y1 = y;
    }
};

// Conservative local bounds from segments. Curves contribute control
// points, arcs pad the end point by radii. Mirrors render semantics
// closely enough for viewport culling.
bool pathLocal(const std::vector<PathSeg>& segs, Box& b) {
    double cx = 0, cy = 0, sx = 0, sy = 0;
    bool started = false;
    for (const auto& g : segs) {
        switch (g.type) {
            case SegType::Move: {
                const double x = g.rel ? cx + g.v[0] : g.v[0];
                const double y = g.rel ? cy + g.v[1] : g.v[1];
                cx = sx = x;
                cy = sy = y;
                started = true;
                b.add(x, y);
                break;
            }
            case SegType::Line:
            case SegType::SmoothQuad: {
                const double x = g.rel ? cx + g.v[0] : g.v[0];
                const double y = g.rel ? cy + g.v[1] : g.v[1];
                cx = x;
                cy = y;
                b.add(x, y);
                break;
            }
            case SegType::H: {
                cx = g.rel ? cx + g.v[0] : g.v[0];
                b.add(cx, cy);
                break;
            }
            case SegType::V: {
                cy = g.rel ? cy + g.v[0] : g.v[0];
                b.add(cx, cy);
                break;
            }
            case SegType::Cubic:
            case SegType::SmoothCubic: {
                for (int k = 0; k < 6; k += 2) {
                    const double x = g.rel ? cx + g.v[k] : g.v[k];
                    const double y = g.rel ? cy + g.v[k + 1] : g.v[k + 1];
                    b.add(x, y);
                }
                cx = g.rel ? cx + g.v[4] : g.v[4];
                cy = g.rel ? cy + g.v[5] : g.v[5];
                break;
            }
            case SegType::Quad: {
                const double x1 = g.rel ? cx + g.v[0] : g.v[0];
                const double y1 = g.rel ? cy + g.v[1] : g.v[1];
                const double x = g.rel ? cx + g.v[2] : g.v[2];
                const double y = g.rel ? cy + g.v[3] : g.v[3];
                b.add(x1, y1);
                b.add(x, y);
                cx = x;
                cy = y;
                break;
            }
            case SegType::Arc: {
                const double x = g.rel ? cx + g.v[5] : g.v[5];
                const double y = g.rel ? cy + g.v[6] : g.v[6];
                b.add(x, y);
                b.add(x - g.v[0], y - g.v[1]);
                b.add(x + g.v[0], y + g.v[1]);
                cx = x;
                cy = y;
                break;
            }
            case SegType::Close:
                cx = sx;
                cy = sy;
                break;
        }
    }
    (void)started;
    return !b.empty;
}

Box xformBox(const Box& b, const Affine& m, double pad) {
    Box o;
    if (b.empty) {
        return o;
    }
    const double xs[4] = {b.x0 - pad, b.x1 + pad, b.x0 - pad, b.x1 + pad};
    const double ys[4] = {b.y0 - pad, b.y1 - pad, b.y1 + pad, b.y0 + pad};
    for (int i = 0; i < 4; ++i) {
        o.add(m.a * xs[i] + m.c * ys[i] + m.e,
              m.b * xs[i] + m.d * ys[i] + m.f);
    }
    return o;
}

// Prototype item bounds: Path gets a real box, all other kinds delegate
// to the engine so the test measures the Path gap only.
Box protoItemBox(const RenderItem& it) {
    if (it.kind != ItemKind::Path) {
        const BBox e = itemBounds(it);
        Box b;
        if (!e.empty) {
            b.empty = false;
            b.x0 = e.x0;
            b.y0 = e.y0;
            b.x1 = e.x1;
            b.y1 = e.y1;
        }
        return b;
    }
    Box local;
    if (!pathLocal(it.segs, local)) {
        return Box{};
    }
    const double pad = it.style.stroke.none ? 0 : it.style.strokeWidth * 0.5;
    return xformBox(local, it.world, pad);
}

bool hitBox(const Box& a, const Box& b) {
    if (a.empty || b.empty) {
        return false;
    }
    return a.x0 <= b.x1 && a.x1 >= b.x0 && a.y0 <= b.y1 && a.y1 >= b.y0;
}

bool sameSegs(const std::vector<PathSeg>& a, const std::vector<PathSeg>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].type != b[i].type || a[i].rel != b[i].rel ||
            a[i].n != b[i].n) {
            return false;
        }
        for (int k = 0; k < a[i].n; ++k) {
            if (std::abs(a[i].v[k] - b[i].v[k]) > 1e-9) {
                return false;
            }
        }
    }
    return true;
}

void collectD(const std::shared_ptr<XmlNode>& n, std::vector<std::string>& out) {
    auto it = n->attrs.find("d");
    if (it != n->attrs.end()) {
        out.push_back(it->second);
    }
    for (const auto& c : n->children) {
        collectD(c, out);
    }
}

}  // namespace

static bool haveFixtures(std::string& sf, std::string& br) {
    const std::string dir = svgDir();
    const std::string a = dir + "/Location_map_San_Francisco_Bay_Area.svg";
    const std::string b = dir + "/Mapa_do_Brasil_por_código_DDD.svg";
    std::string t;
    bool oka = loadFile(a, t);
    bool okb = loadFile(b, t);
    sf = a;
    br = b;
    return oka || okb;
}

static void test_census() {
    std::string sf, br;
    if (!haveFixtures(sf, br)) {
        std::printf("[large] fixtures absent (PITTORE_SVG_DIR=%s) — skipped\n",
                    svgDir().c_str());
        return;
    }
    for (const char* name : {sf.c_str(), br.c_str()}) {
        std::string xml;
        if (!loadFile(name, xml)) {
            std::printf("[large] missing %s — skipped\n", name);
            continue;
        }
        const size_t paths = countNeedle(xml, "<path");
        const size_t groups = countNeedle(xml, "<g");
        const size_t styles = countNeedle(xml, "style=");
        const size_t xforms = countNeedle(xml, "transform=");
        std::printf("[large] %s bytes=%zu paths=%zu groups=%zu style=%zu "
                    "xform=%zu\n",
                    name, xml.size(), paths, groups, styles, xforms);
        CHECK(!xml.empty());
        CHECK(paths > 1000);
    }
    std::string xml;
    if (loadFile(sf, xml)) {
        CHECK(countNeedle(xml, "<path") > 50000);
    }
    if (loadFile(br, xml)) {
        CHECK(countNeedle(xml, "<path") > 3000);
    }
}

static void test_baseline() {
    std::string sf, br;
    if (!haveFixtures(sf, br)) {
        return;
    }
    for (const char* name : {sf.c_str(), br.c_str()}) {
        std::string xml;
        if (!loadFile(name, xml)) {
            continue;
        }
        const double t0 = nowMs();
        const XmlRead r = readXml(xml);
        const double t1 = nowMs();
        CHECK(r.ok);
        if (!r.ok) {
            continue;
        }
        std::vector<CssRule> sheet;
        for (const auto& c : r.root->children) {
            if (c->tag == "style") {
                for (const auto& q : parseStylesheet(c->text)) {
                    sheet.push_back(q);
                }
            }
        }
        const double t2 = nowMs();
        const Scene sc = buildScene(*r.root, sheet);
        const double t3 = nowMs();
        CHECK(sc.ok);
        const std::vector<RenderItem> items = flattenScene(sc);
        const double t4 = nowMs();
        size_t empty = 0;
        for (const auto& it : items) {
            if (itemBounds(it).empty) {
                ++empty;
            }
        }
        const double t5 = nowMs();
        std::printf("[large] base %s xml=%.0fms scene=%.0fms flat=%.0fms "
                    "bounds=%.0fms items=%zu empty=%zu (%.1f%%)\n",
                    name, t1 - t0, t3 - t2, t4 - t3, t5 - t4, items.size(),
                    empty,
                    items.empty() ? 0 : 100.0 * empty / items.size());
        CHECK(!items.empty());
    }
}

static void test_number_proto() {
    const char* cases[] = {"1",         "-2.5",  ".5",   "1e3",   "-1E-2",
                           "3.",        "+.25", "10-20", "1,2,3", "0.1 0.2"};
    for (const char* c : cases) {
        std::vector<double> want;
        {
            // Reference: current comma/blank splitter path is not public,
            // so compare against strtod loop with identical skipping.
            const std::string s(c);
            size_t i = 0;
            while (i < s.size()) {
                while (i < s.size() &&
                       (s[i] == ' ' || s[i] == ',' || s[i] == '\t' ||
                        s[i] == '\n')) {
                    ++i;
                }
                if (i >= s.size()) {
                    break;
                }
                char* end = nullptr;
                const double v = std::strtod(s.c_str() + i, &end);
                if (end == s.c_str() + i) {
                    break;
                }
                want.push_back(v);
                i = (size_t)(end - s.c_str());
            }
        }
        std::vector<double> got;
        fastDoubles(c, got);
        CHECK_EQ(got.size(), want.size());
        for (size_t i = 0; i < got.size() && i < want.size(); ++i) {
            CHECK(std::abs(got[i] - want[i]) <= 1e-9);
        }
    }
    // Bench on 200k synthetic numbers.
    std::string big;
    big.reserve(1400000);
    for (int i = 0; i < 200000; ++i) {
        big += "-12.5,";
    }
    std::vector<double> ref;
    double t0 = nowMs();
    {
        size_t i = 0;
        while (i < big.size()) {
            while (i < big.size() && (big[i] == ' ' || big[i] == ',')) {
                ++i;
            }
            if (i >= big.size()) {
                break;
            }
            char* end = nullptr;
            const double v = std::strtod(big.c_str() + i, &end);
            if (end == big.c_str() + i) {
                break;
            }
            ref.push_back(v);
            i = (size_t)(end - big.c_str());
        }
    }
    const double t1 = nowMs();
    std::vector<double> fast;
    fastDoubles(big, fast);
    const double t2 = nowMs();
    CHECK_EQ(fast.size(), ref.size());
    std::printf("[large] doubles n=%zu strtod=%.0fms fast=%.0fms\n", ref.size(),
                t1 - t0, t2 - t1);
}

static void test_path_proto() {
    const char* cases[] = {
        "M0 0 L10 0 H5 V5 C0 0 1 1 2 2 Z",
        "m0 0 l1 1",
        "M0 0L1 1L2 2",
        "M10 20v-5h3",
        "M0 0 C1 2 3 4 5 6 S7 8 9 10 Q11 12 13 14 T15 16 A1 2 0 0 1 3 4 Z",
        "M0 0 L",
        "M1 2 M3 4",
        "M356.775,483.246 l-0.094-0.005l-0.05-0.012",
        "",
    };
    for (const char* c : cases) {
        const std::vector<PathSeg> want = parsePathData(c);
        std::vector<PathSeg> got;
        fastPath(c, got);
        CHECK(sameSegs(got, want));
    }
    // Robustness: bad input must return quickly, never hang.
    for (const char* c : {"Camada_1", "Cxyz", "M", "zzz", "L10", "M0 0 L"}) {
        std::vector<PathSeg> got;
        const double t0 = nowMs();
        fastPath(c, got);
        CHECK(nowMs() - t0 < 1000);
        (void)got;
    }
    std::string sf, br;
    if (!haveFixtures(sf, br)) {
        return;
    }
    for (const char* name : {sf.c_str(), br.c_str()}) {
        std::string xml;
        if (!loadFile(name, xml)) {
            continue;
        }
        const XmlRead r = readXml(xml);
        if (!r.ok) {
            continue;
        }
        std::vector<std::string> ds;
        collectD(r.root, ds);
        const size_t n = ds.size() > 3000 ? 3000 : ds.size();
        double t0 = nowMs();
        size_t s0 = 0;
        for (size_t i = 0; i < n; ++i) {
            s0 += parsePathData(ds[i]).size();
        }
        const double t1 = nowMs();
        size_t s1 = 0;
        for (size_t i = 0; i < n; ++i) {
            std::vector<PathSeg> v;
            fastPath(ds[i], v);
            s1 += v.size();
        }
        const double t2 = nowMs();
        CHECK_EQ(s0, s1);
        std::printf("[large] path n=%zu segs=%zu cur=%.0fms proto=%.0fms\n", n,
                    s0, t1 - t0, t2 - t1);
    }
}

static void test_bounds_proto() {
    // Gap closed: the engine now returns real Path boxes (pathWorldBox).
    RenderItem path;
    path.kind = ItemKind::Path;
    path.segs = parsePathData("M10 20 L30 40");
    CHECK(!itemBounds(path).empty);
    const Box pb = protoItemBox(path);
    CHECK(!pb.empty);
    CHECK(std::abs(pb.x0 - 10) <= 1e-9);
    CHECK(std::abs(pb.y0 - 20) <= 1e-9);
    CHECK(std::abs(pb.x1 - 30) <= 1e-9);
    CHECK(std::abs(pb.y1 - 40) <= 1e-9);
    // Non-path kinds delegate exactly.
    RenderItem rect;
    rect.kind = ItemKind::Rect;
    rect.x = 1;
    rect.y = 2;
    rect.w = 10;
    rect.h = 20;
    const BBox eb = itemBounds(rect);
    const Box qb = protoItemBox(rect);
    CHECK(!eb.empty && !qb.empty);
    CHECK(std::abs(qb.x0 - eb.x0) <= 1e-9);
    CHECK(std::abs(qb.x1 - eb.x1) <= 1e-9);

    std::string sf, br;
    if (!haveFixtures(sf, br)) {
        return;
    }
    for (const char* name : {sf.c_str(), br.c_str()}) {
        std::string xml;
        if (!loadFile(name, xml)) {
            continue;
        }
        const XmlRead r = readXml(xml);
        if (!r.ok) {
            continue;
        }
        const Scene sc = buildScene(*r.root, {});
        const std::vector<RenderItem> items = flattenScene(sc);
        size_t curEmpty = 0, protoEmpty = 0;
        Box doc;
        std::vector<Box> boxes;
        boxes.reserve(items.size());
        for (const auto& it : items) {
            if (itemBounds(it).empty) {
                ++curEmpty;
            }
            const Box b = protoItemBox(it);
            if (b.empty) {
                ++protoEmpty;
            } else {
                if (doc.empty) {
                    doc = b;
                } else {
                    Box u = doc;
                    if (b.x0 < u.x0) u.x0 = b.x0;
                    if (b.y0 < u.y0) u.y0 = b.y0;
                    if (b.x1 > u.x1) u.x1 = b.x1;
                    if (b.y1 > u.y1) u.y1 = b.y1;
                    doc = u;
                }
            }
            boxes.push_back(b);
        }
        const double w = doc.x1 - doc.x0, h = doc.y1 - doc.y0;
        const Box center{doc.x0 + w / 4, doc.y0 + h / 4, doc.x0 + 3 * w / 4,
                         doc.y0 + 3 * h / 4, false};
        size_t keep = 0;
        for (const auto& b : boxes) {
            if (hitBox(b, center)) {
                ++keep;
            }
        }
        const double culled =
            boxes.empty() ? 0 : 100.0 * (boxes.size() - keep) / boxes.size();
        std::printf("[large] cull %s items=%zu curEmpty=%zu protoEmpty=%zu "
                    "centerKeeps=%zu culled=%.1f%%\n",
                    name, items.size(), curEmpty, protoEmpty, keep, culled);
        CHECK_EQ(protoEmpty, curEmpty);
        CHECK(culled > 30);
    }
}

int main() {
    test_census();
    test_baseline();
    test_number_proto();
    test_path_proto();
    test_bounds_proto();
    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}
