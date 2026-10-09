// Style resolve. One lookup per prop, parent fallback.
#include "engine/vector/svg/style.h"

#include <cstdlib>

#include "engine/vector/svg/css_parse.h"
#include "engine/vector/svg/length.h"
#include "engine/vector/svg/xml_reader.h"

namespace pittore::svg {
namespace {

// Raw attr or style="" value. Sheet wins later.
std::string rawProp(const XmlNode& n, const std::vector<CssRule>& sheet,
                    const std::string& prop) {
    std::string out;
    bool has = false;
    if (auto it = n.attrs.find(prop); it != n.attrs.end()) {
        out = it->second;
        has = true;
    }
    if (auto it = n.attrs.find("style"); it != n.attrs.end()) {
        for (const auto& [k, v] : parseStyleDecls(it->second)) {
            if (k == prop) {
                out = v;
                has = true;
            }
        }
    }
    // Match tag, .class, #id, tag.class, *.
    auto clsHas = [&](const std::string& list, const std::string& c) {
        size_t i = 0;
        while (i < list.size()) {
            while (i < list.size() && list[i] == ' ') {
                ++i;
            }
            size_t j = i;
            while (j < list.size() && list[j] != ' ') {
                ++j;
            }
            if (j > i && list.substr(i, j - i) == c) {
                return true;
            }
            i = j;
        }
        return false;
    };
    auto match = [&](const std::string& sel) {
        if (sel.empty() || sel == "*") {
            return true;
        }
        if (sel[0] == '#') {
            auto it = n.attrs.find("id");
            return it != n.attrs.end() && it->second == sel.substr(1);
        }
        if (sel[0] == '.') {
            auto it = n.attrs.find("class");
            return it != n.attrs.end() && clsHas(it->second, sel.substr(1));
        }
        const size_t dot = sel.find('.');
        if (dot != std::string::npos) {
            if (n.tag != sel.substr(0, dot)) {
                return false;
            }
            auto it = n.attrs.find("class");
            return it != n.attrs.end() &&
                   clsHas(it->second, sel.substr(dot + 1));
        }
        return n.tag == sel;
    };
    for (const auto& r : sheet) {
        if (!match(r.selector)) {
            continue;
        }
        auto it = r.decls.find(prop);
        if (it != r.decls.end()) {
            out = it->second;
            has = true;
        }
    }
    return has ? out : std::string();
}

double numOr(const std::string& s, double fb) {
    if (s.empty()) {
        return fb;
    }
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    return end == s.c_str() ? fb : v;
}

}  // namespace

Style resolveStyle(const XmlNode& n, const std::vector<CssRule>& sheet,
                   const Style* parent) {
    Style s;
    if (parent) {
        s = *parent;
    } else {
        s.fill.none = true;
        s.stroke.none = true;
    }
    const std::string fill = rawProp(n, sheet, "fill");
    if (!fill.empty()) {
        s.fill = parsePaint(fill);
    } else if (!parent) {
        // Default fill is black when no parent sets it.
        float c[4] = {0, 0, 0, 1};
        if (parseColor("black", c)) {
            s.fill.none = false;
            s.fill.r = c[0];
            s.fill.g = c[1];
            s.fill.b = c[2];
            s.fill.a = c[3];
        }
    }
    const std::string stroke = rawProp(n, sheet, "stroke");
    if (!stroke.empty()) {
        s.stroke = parsePaint(stroke);
    }
    const std::string op = rawProp(n, sheet, "opacity");
    if (!op.empty()) {
        s.opacity = numOr(op, 1.0);
    } else if (!parent) {
        s.opacity = 1.0;
    }
    const std::string fo = rawProp(n, sheet, "fill-opacity");
    if (!fo.empty()) {
        s.fillOpacity = numOr(fo, 1.0);
    } else if (!parent) {
        s.fillOpacity = 1.0;
    }
    const std::string so = rawProp(n, sheet, "stroke-opacity");
    if (!so.empty()) {
        s.strokeOpacity = numOr(so, 1.0);
    } else if (!parent) {
        s.strokeOpacity = 1.0;
    }
    const std::string sw = rawProp(n, sheet, "stroke-width");
    if (!sw.empty()) {
        s.strokeWidth = readLength(sw, 100.0, s.strokeWidth);
    } else if (!parent) {
        s.strokeWidth = 1.0;
    }
    const std::string disp = rawProp(n, sheet, "display");
    if (!disp.empty()) {
        s.displayNone = disp == "none";
    }
    const std::string clip = rawProp(n, sheet, "clip-path");
    if (!clip.empty()) {
        s.clipRef = paintRef(clip);
    }
    const std::string mask = rawProp(n, sheet, "mask");
    if (!mask.empty()) {
        s.maskRef = paintRef(mask);
    }
    const std::string filt = rawProp(n, sheet, "filter");
    if (!filt.empty()) {
        s.filterRef = paintRef(filt);
    }
    return s;
}

}  // namespace pittore::svg
