// DOM to scene. One builder per tag, shared matrix stack.
#include "engine/vector/svg/scene.h"

#include <cstdlib>

#include "engine/vector/svg/css_parse.h"
#include "engine/vector/svg/length.h"
#include "engine/vector/svg/path_scan.h"
#include "engine/vector/svg/xml_reader.h"

namespace pittore::svg {
namespace {

double attrLen(const XmlNode& n, const char* key, double ref, double fb) {
    auto it = n.attrs.find(key);
    if (it == n.attrs.end()) {
        return fb;
    }
    const Length l = parseLength(it->second);
    return l.valid ? toPx(l, ref) : fb;
}

std::string attrStr(const XmlNode& n, const char* key) {
    auto it = n.attrs.find(key);
    return it == n.attrs.end() ? std::string() : it->second;
}

// viewBox "a b w h" widths.
bool parseViewBox(const std::string& s, double& w, double& h) {
    char* end = nullptr;
    const char* p = s.c_str();
    double v[4] = {0, 0, 0, 0};
    for (int k = 0; k < 4; ++k) {
        v[k] = std::strtod(p, &end);
        if (end == p) {
            return false;
        }
        p = end;
    }
    w = v[2];
    h = v[3];
    return w > 0 && h > 0;
}

NodeKind kindFor(const std::string& tag) {
    if (tag == "rect") {
        return NodeKind::Rect;
    }
    if (tag == "circle") {
        return NodeKind::Circle;
    }
    if (tag == "ellipse") {
        return NodeKind::Ellipse;
    }
    if (tag == "line") {
        return NodeKind::Line;
    }
    if (tag == "polyline") {
        return NodeKind::Polyline;
    }
    if (tag == "polygon") {
        return NodeKind::Polygon;
    }
    if (tag == "path") {
        return NodeKind::Path;
    }
    if (tag == "use") {
        return NodeKind::Use;
    }
    if (tag == "image") {
        return NodeKind::Image;
    }
    if (tag == "text" || tag == "tspan") {
        return NodeKind::Text;
    }
    return NodeKind::Group;
}

// href, xlink:href, or plain id.
std::string nodeHref(const XmlNode& n) {
    for (const char* k : {"href", "xlink:href"}) {
        auto it = n.attrs.find(k);
        if (it != n.attrs.end() && !it->second.empty()) {
            if (it->second[0] == '#') {
                return it->second.substr(1);
            }
            const size_t h = it->second.find('#');
            if (h != std::string::npos) {
                return it->second.substr(h + 1);
            }
            return it->second;
        }
    }
    return "";
}

// Split points "x,y x,y ...".
std::vector<std::pair<double, double>> parsePoints(const std::string& s) {
    std::vector<std::pair<double, double>> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() &&
               (s[i] == ' ' || s[i] == ',' || s[i] == '\t' || s[i] == '\n')) {
            ++i;
        }
        if (i >= s.size()) {
            break;
        }
        char* end = nullptr;
        const double x = std::strtod(s.c_str() + i, &end);
        if (end == s.c_str() + i) {
            break;
        }
        i = (size_t)(end - s.c_str());
        while (i < s.size() &&
               (s[i] == ' ' || s[i] == ',' || s[i] == '\t' || s[i] == '\n')) {
            ++i;
        }
        const double y = std::strtod(s.c_str() + i, &end);
        if (end == s.c_str() + i) {
            break;
        }
        i = (size_t)(end - s.c_str());
        out.emplace_back(x, y);
    }
    return out;
}

void fillGeom(const XmlNode& n, SceneNode& s, double vw, double vh) {
    switch (s.kind) {
        case NodeKind::Rect:
            s.x = attrLen(n, "x", vw, 0);
            s.y = attrLen(n, "y", vh, 0);
            s.w = attrLen(n, "width", vw, 0);
            s.h = attrLen(n, "height", vh, 0);
            s.rx = attrLen(n, "rx", vw, 0);
            s.ry = attrLen(n, "ry", vh, s.rx);
            break;
        case NodeKind::Circle:
            s.cx = attrLen(n, "cx", vw, 0);
            s.cy = attrLen(n, "cy", vh, 0);
            s.r = attrLen(n, "r", vw < vh ? vw : vh, 0);
            break;
        case NodeKind::Ellipse:
            s.cx = attrLen(n, "cx", vw, 0);
            s.cy = attrLen(n, "cy", vh, 0);
            s.rx = attrLen(n, "rx", vw, 0);
            s.ry = attrLen(n, "ry", vh, 0);
            break;
        case NodeKind::Line:
            s.x1 = attrLen(n, "x1", vw, 0);
            s.y1 = attrLen(n, "y1", vh, 0);
            s.x2 = attrLen(n, "x2", vw, 0);
            s.y2 = attrLen(n, "y2", vh, 0);
            break;
        case NodeKind::Polyline:
        case NodeKind::Polygon:
            s.points = parsePoints(attrStr(n, "points"));
            break;
        case NodeKind::Path:
            s.segs.clear();
            scanPathData(attrStr(n, "d"), s.segs);
            break;
        case NodeKind::Use:
            s.href = nodeHref(n);
            s.x = attrLen(n, "x", vw, 0);
            s.y = attrLen(n, "y", vh, 0);
            s.w = attrLen(n, "width", vw, 0);
            s.h = attrLen(n, "height", vh, 0);
            break;
        case NodeKind::Image:
            s.href = nodeHref(n);
            s.x = attrLen(n, "x", vw, 0);
            s.y = attrLen(n, "y", vh, 0);
            s.w = attrLen(n, "width", vw, 0);
            s.h = attrLen(n, "height", vh, 0);
            break;
        case NodeKind::Text:
            s.x = attrLen(n, "x", vw, 0);
            s.y = attrLen(n, "y", vh, 0);
            s.text = n.text;
            break;
        default:
            break;
    }
}

std::shared_ptr<SceneNode> buildNode(const XmlNode& n,
                                     const std::vector<CssRule>& sheet,
                                     const Affine& parentWorld,
                                     const Style* parentStyle, double vw,
                                     double vh) {
    auto s = std::make_shared<SceneNode>();
    s->kind = kindFor(n.tag);
    s->id = attrStr(n, "id");
    auto ti = n.attrs.find("transform");
    s->local = ti == n.attrs.end() ? identity() : parseTransform(ti->second);
    s->world = multiply(parentWorld, s->local);
    s->style = resolveStyle(n, sheet, parentStyle);
    // Defs children are kept but hidden from paint.
    if (n.tag == "defs" || n.tag == "clipPath" || n.tag == "mask" ||
        n.tag == "pattern" || n.tag == "marker" || n.tag == "linearGradient" ||
        n.tag == "radialGradient") {
        s->kind = NodeKind::Group;
    }
    fillGeom(n, *s, vw, vh);
    for (const auto& c : n.children) {
        // Skip nested style text nodes from paint tree.
        if (c->tag == "style") {
            continue;
        }
        s->children.push_back(buildNode(*c, sheet, s->world, &s->style, vw, vh));
    }
    // Fold tspan text into parent text node.
    if (s->kind == NodeKind::Text) {
        for (const auto& c : n.children) {
            if (c->tag == "tspan") {
                s->text += c->text;
            }
        }
    }
    return s;
}

}  // namespace

Scene buildScene(const XmlNode& root, const std::vector<CssRule>& sheet) {
    Scene out;
    double vw = 0, vh = 0;
    auto vi = root.attrs.find("viewBox");
    if (vi != root.attrs.end()) {
        parseViewBox(vi->second, vw, vh);
    }
    if (vw <= 0 || vh <= 0) {
        vw = attrLen(root, "width", 1000, 0);
        vh = attrLen(root, "height", 1000, 0);
    }
    if (vw <= 0) {
        vw = 1000;
    }
    if (vh <= 0) {
        vh = 1000;
    }
    out.viewW = vw;
    out.viewH = vh;
    out.root = buildNode(root, sheet, identity(), nullptr, vw, vh);
    out.ok = out.root != nullptr;
    return out;
}

}  // namespace pittore::svg
