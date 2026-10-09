// Clone / use / symbol expansion.
#include "engine/vector/clone.h"

#include <cmath>
#include <functional>
#include <sstream>

namespace pittore::vector {
namespace {

std::string suffixId(const std::string& id, const std::string& suffix) {
    return id.empty() ? id : id + suffix;
}

std::shared_ptr<SvgElement> copyTree(const SvgElement& el,
                                     const std::string& suffix) {
    auto out = std::make_shared<SvgElement>();
    out->tag = el.tag;
    out->attrs = el.attrs;
    if (auto id = out->get("id")) out->set("id", suffixId(*id, suffix));
    out->text = el.text;
    for (auto& c : el.children) {
        auto cc = copyTree(*c, suffix);
        cc->parent = out.get();
        out->children.push_back(cc);
    }
    return out;
}

}  // namespace

std::shared_ptr<SvgElement> deepCopyElement(const SvgElement& el,
                                            const std::string& idSuffix) {
    return copyTree(el, idSuffix);
}

std::optional<std::shared_ptr<SvgElement>> expandUse(const SvgElement& use,
                                                     const SvgDocument& doc,
                                                     const std::string& idSuffix) {
    std::string href;
    if (auto h = use.get("href")) href = *h;
    if (auto h = use.get("xlink:href"); href.empty() && h) href = *h;
    std::string target = normalizeIri(href);
    if (target.empty()) return std::nullopt;
    SvgElement* ref = doc.findId(target);
    if (!ref) return std::nullopt;
    // <symbol> unwraps to a <g> (viewBox handling stays with the caller).
    auto copy = copyTree(*ref, idSuffix);
    if (copy->tag == "symbol") copy->tag = "g";
    double x = 0, y = 0;
    try {
        if (auto v = use.get("x")) x = std::stod(*v);
        if (auto v = use.get("y")) y = std::stod(*v);
    } catch (...) {
    }
    std::ostringstream pre;
    if (x != 0 || y != 0) pre << "translate(" << x << " " << y << ")";
    if (auto t = use.get("transform"); t && !t->empty()) {
        if (pre.tellp() > 0) pre << " ";
        pre << *t;
    }
    if (!pre.str().empty()) {
        auto wrap = std::make_shared<SvgElement>();
        wrap->tag = "g";
        wrap->set("transform", pre.str());
        copy->parent = wrap.get();
        wrap->children.push_back(copy);
        // Carry over presentation attrs from the <use>.
        for (const auto& [k, v] : use.attrs) {
            if (k == "href" || k == "xlink:href" || k == "x" || k == "y" ||
                k == "transform")
                continue;
            if (k == "id") {
                wrap->set("id", v + idSuffix);
                continue;
            }
            if (!wrap->get(k)) wrap->set(k, v);
        }
        return wrap;
    }
    return copy;
}

int expandAllUses(SvgDocument& doc) {
    int count = 0;
    for (int pass = 0; pass < 16; pass++) {
        bool any = false;
        std::function<void(std::shared_ptr<SvgElement>)> walk =
            [&](std::shared_ptr<SvgElement> el) {
                for (size_t i = 0; i < el->children.size(); i++) {
                    auto child = el->children[i];
                    if (child->tag == "use") {
                        if (auto ex = expandUse(*child, doc, "-u" + std::to_string(pass))) {
                            (*ex)->parent = el.get();
                            el->children[i] = *ex;
                            count++;
                            any = true;
                        }
                    } else {
                        walk(child);
                    }
                }
            };
        if (doc.root) walk(doc.root);
        doc.reindex();
        if (!any) break;
    }
    return count;
}

int unlinkClones(SvgDocument& doc, bool deep) {
    (void)deep;
    return expandAllUses(doc);
}

std::vector<std::shared_ptr<SvgElement>> tileClones(const SvgElement& source,
                                                    const TileSpec& spec) {
    std::vector<std::shared_ptr<SvgElement>> out;
    auto stamp = [&](double tx, double ty, double rot, double sc) {
        auto g = std::make_shared<SvgElement>();
        g->tag = "g";
        std::ostringstream t;
        t << "translate(" << tx << " " << ty << ")";
        if (rot != 0) t << " rotate(" << rot << ")";
        if (sc != 1.0 && sc > 0) t << " scale(" << sc << ")";
        g->set("transform", t.str());
        auto c = copyTree(source, "-t" + std::to_string(out.size()));
        c->parent = g.get();
        g->children.push_back(c);
        out.push_back(g);
    };
    if (spec.kind == TileKind::Grid) {
        int n = 0;
        for (int r = 0; r < spec.rows; r++)
            for (int c = 0; c < spec.cols; c++) {
                double sc = 1.0 + spec.scaleStep * c;
                stamp(c * spec.dx, r * spec.dy, spec.rotateStep * n, sc);
                n++;
            }
    } else if (spec.kind == TileKind::Radial) {
        for (int k = 0; k < spec.radialCount; k++) {
            double a = 2 * 3.14159265358979 * k / (spec.radialCount > 0 ? spec.radialCount : 1);
            stamp(spec.radialR * std::cos(a), spec.radialR * std::sin(a),
                  a * 180.0 / 3.14159265358979 + spec.rotateStep * k, 1.0);
        }
    } else {
        for (int k = 0; k < spec.radialCount; k++) {
            double a = 2 * 3.14159265358979 * k / (spec.radialCount > 0 ? spec.radialCount : 1);
            double r = spec.radialR * (k + 1) / (spec.radialCount > 0 ? spec.radialCount : 1);
            stamp(r * std::cos(a), r * std::sin(a), spec.rotateStep * k,
                  1.0 + spec.scaleStep * k);
        }
    }
    return out;
}

std::vector<UseLink> listUseLinks(SvgDocument& doc) {
    std::vector<UseLink> out;
    if (!doc.root) return out;
    std::function<void(SvgElement*)> walk = [&](SvgElement* el) {
        if (el->tag == "use") {
            std::string href;
            if (auto h = el->get("href")) href = *h;
            if (auto h = el->get("xlink:href"); href.empty() && h) href = *h;
            std::string t = normalizeIri(href);
            out.push_back(UseLink{el, t, doc.findId(t) != nullptr});
        }
        for (auto& c : el->children) walk(c.get());
    };
    walk(doc.root.get());
    return out;
}

}  // namespace pittore::vector
