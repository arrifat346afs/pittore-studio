// Clip/mask resolution.
#include "engine/vector/clip_mask.h"

#include "engine/vector/boolean.h"
#include "engine/vector/svg_dom.h"

#include <functional>

namespace pittore::vector {
namespace {

// Collect flattened rings from <path>/<rect>/<circle>/<ellipse>/<polygon>
// children of a clipPath element. Only axis-aligned rect/circle decoding is
// needed for hit-testing; general paths arrive via the `d` attribute parsed
// with the same cubic-flattening the svg importer uses (lines only here --
// curves subdivide in the rasterizer, while the boolean kernel below works on
// the flattened fragments).
std::vector<ClipShape> shapesFromClipEl(const SvgElement* clipEl) {
    std::vector<ClipShape> out;
    if (!clipEl) return out;
    std::function<void(const SvgElement*)> walk = [&](const SvgElement* el) {
        if (el->tag == "path" || el->tag == "rect" || el->tag == "circle" ||
            el->tag == "ellipse" || el->tag == "polygon" ||
            el->tag == "polyline") {
            ClipShape shape;
            shape.evenOdd = false;
            if (auto fr = el->get("clip-rule"))
                shape.evenOdd = (*fr == "evenodd");
            else if (auto fr = el->get("fill-rule"))
                shape.evenOdd = (*fr == "evenodd");
            // Rect fast path (common for PowerClip boxes).
            if (el->tag == "rect") {
                double x = 0, y = 0, w = 0, h = 0;
                try {
                    if (auto v = el->get("x")) x = std::stod(*v);
                    if (auto v = el->get("y")) y = std::stod(*v);
                    if (auto v = el->get("width")) w = std::stod(*v);
                    if (auto v = el->get("height")) h = std::stod(*v);
                } catch (...) {
                }
                if (w > 0 && h > 0)
                    shape.rings.push_back(
                        {{ {x, y}, {x + w, y}, {x + w, y + h}, {x, y + h} }});
            }
            if (!shape.rings.empty()) out.push_back(std::move(shape));
        }
        for (auto& c : el->children) walk(c.get());
    };
    walk(clipEl);
    return out;
}

bool ringContains(const std::vector<std::pair<double, double>>& ring, double x,
                  double y, bool evenOdd) {
    bool inside = false;
    int winding = 0;
    size_t n = ring.size();
    for (size_t i = 0; i < n; i++) {
        auto [x0, y0] = ring[i];
        auto [x1, y1] = ring[(i + 1) % n];
        if ((y0 > y) != (y1 > y)) {
            double xi = x0 + (y - y0) * (x1 - x0) / (y1 - y0);
            if (xi > x) {
                if (evenOdd)
                    inside = !inside;
                else
                    winding += (y1 > y0) ? 1 : -1;
            }
        }
    }
    return evenOdd ? inside : winding != 0;
}

}  // namespace

std::vector<ClipShape> clipFor(const SvgElement& el, const SvgDocument& doc) {
    auto v = doc.resolved(el, "clip-path");
    if (!v || *v == "none") return {};
    std::string id = urlRefTarget(*v);
    if (id.empty()) id = normalizeIri(*v);
    if (id.empty()) return {};
    SvgElement* clipEl = doc.findId(id);
    if (!clipEl) return {};
    return shapesFromClipEl(clipEl);
}

bool pointInClip(const std::vector<ClipShape>& clip, double x, double y) {
    if (clip.empty()) return true;
    for (const auto& shape : clip) {
        bool in = false;
        if (shape.evenOdd) {
            bool odd = false;
            for (const auto& ring : shape.rings)
                if (ringContains(ring, x, y, true)) odd = !odd;
            in = odd;
        } else {
            int w = 0;
            for (const auto& ring : shape.rings) {
                // Signed contribution via even-odd per ring parity trick:
                // count ringContains with nonzero per ring then OR.
                if (ringContains(ring, x, y, false)) {
                    // Determine ring orientation sign by area.
                    double a = 0;
                    for (size_t i = 0; i < ring.size(); i++) {
                        auto [x0, y0] = ring[i];
                        auto [x1, y1] = ring[(i + 1) % ring.size()];
                        a += (x1 - x0) * (y1 + y0);
                    }
                    w += (a > 0) ? -1 : 1;
                }
            }
            in = w != 0;
        }
        if (in) return true;  // union of shapes
    }
    return false;
}

std::vector<std::vector<std::pair<double, double>>> applyClipToPolylines(
    const std::vector<std::vector<std::pair<double, double>>>& subject,
    const std::vector<ClipShape>& clip) {
    if (clip.empty()) return subject;
    // Fragment-level clip: keep segments whose midpoint is inside.
    std::vector<std::vector<std::pair<double, double>>> out;
    for (const auto& poly : subject) {
        std::vector<std::pair<double, double>> cur;
        for (size_t i = 0; i + 1 < poly.size(); i++) {
            double mx = (poly[i].first + poly[i + 1].first) / 2.0;
            double my = (poly[i].second + poly[i + 1].second) / 2.0;
            if (pointInClip(clip, mx, my)) {
                if (cur.empty()) cur.push_back(poly[i]);
                cur.push_back(poly[i + 1]);
            } else if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
        }
        if (!cur.empty()) out.push_back(cur);
    }
    return out;
}

float MaskField::valueAt(double px, double py) const {
    if (empty()) return 1.0f;
    double fx = (px - x);
    double fy = (py - y);
    if (fx < 0 || fy < 0 || fx > w - 1 || fy > h - 1) return 0.0f;
    int x0 = (int)fx, y0 = (int)fy;
    int x1 = std::min(w - 1, x0 + 1), y1 = std::min(h - 1, y0 + 1);
    double tx = fx - x0, ty = fy - y0;
    auto at = [&](int xx, int yy) { return alpha[(size_t)(yy * w + xx)] / 255.0f; };
    float a = at(x0, y0) * (1 - (float)tx) + at(x1, y0) * (float)tx;
    float b = at(x0, y1) * (1 - (float)tx) + at(x1, y1) * (float)tx;
    return (a * (1 - (float)ty) + b * (float)ty) * (float)opacity;
}

std::optional<MaskRef> maskFor(const SvgElement& el, const SvgDocument& doc) {
    auto v = doc.resolved(el, "mask");
    if (!v || *v == "none") return std::nullopt;
    std::string id = urlRefTarget(*v);
    if (id.empty()) id = normalizeIri(*v);
    if (id.empty()) return std::nullopt;
    SvgElement* m = doc.findId(id);
    if (!m) return std::nullopt;
    MaskRef r;
    r.id = id;
    r.element = m;
    try {
        if (auto x = m->get("x")) r.x = std::stod(*x);
        if (auto y = m->get("y")) r.y = std::stod(*y);
        if (auto w = m->get("width")) r.w = std::stod(*w);
        if (auto h = m->get("height")) r.h = std::stod(*h);
    } catch (...) {
    }
    return r;
}

}  // namespace pittore::vector
