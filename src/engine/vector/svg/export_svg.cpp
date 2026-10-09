// Minimal scene emit. Rects, circles, paths, text.
#include "engine/vector/svg/export_svg.h"

#include <cstdio>

#include "engine/vector/svg/render_item.h"
#include "engine/vector/svg/scene.h"

namespace pittore::svg {
namespace {

std::string num(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%g", v);
    return b;
}

}  // namespace

std::string emitSceneSvg(const Scene& scene) {
    if (!scene.ok || !scene.root) {
        return "";
    }
    std::string out = "<svg width=\"" + num(scene.viewW) + "\" height=\"" +
                      num(scene.viewH) + "\" viewBox=\"0 0 " +
                      num(scene.viewW) + " " + num(scene.viewH) + "\">\n";
    const auto items = flattenScene(scene);
    for (const auto& it : items) {
        if (it.kind == ItemKind::Group) {
            continue;
        }
        if (it.kind == ItemKind::Rect) {
            out += "  <rect x=\"" + num(it.x) + "\" y=\"" + num(it.y) +
                   "\" width=\"" + num(it.w) + "\" height=\"" + num(it.h) +
                   "\"/>\n";
        } else if (it.kind == ItemKind::Circle) {
            out += "  <circle cx=\"" + num(it.cx) + "\" cy=\"" + num(it.cy) +
                   "\" r=\"" + num(it.r) + "\"/>\n";
        } else if (it.kind == ItemKind::Path) {
            out += "  <path d=\"";
            for (const auto& s : it.segs) {
                if (s.type == SegType::Move) {
                    out += "M" + num(s.v[0]) + " " + num(s.v[1]) + " ";
                } else if (s.type == SegType::Line) {
                    out += "L" + num(s.v[0]) + " " + num(s.v[1]) + " ";
                } else if (s.type == SegType::Close) {
                    out += "Z ";
                } else if (s.type == SegType::Cubic) {
                    out += "C" + num(s.v[0]) + " " + num(s.v[1]) + " " +
                           num(s.v[2]) + " " + num(s.v[3]) + " " + num(s.v[4]) +
                           " " + num(s.v[5]) + " ";
                }
            }
            out += "\"/>\n";
        } else if (it.kind == ItemKind::Text) {
            out += "  <text x=\"" + num(it.x) + "\" y=\"" + num(it.y) + "\">" +
                   it.text + "</text>\n";
        }
    }
    out += "</svg>\n";
    return out;
}

}  // namespace pittore::svg
