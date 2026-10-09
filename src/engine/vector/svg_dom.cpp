// SVG DOM: tree plus cascade. Parse via shared modules.
#include "engine/vector/svg_dom.h"

#include <cctype>
#include <functional>

#include "engine/vector/svg/css_parse.h"
#include "engine/vector/svg/iri.h"
#include "engine/vector/svg/xml_reader.h"

namespace pittore::vector {
namespace {

// Escape for save.
std::string encodeAttr(const std::string& in) {
    std::string out;
    for (char c : in) {
        if (c == '&') {
            out += "&amp;";
        } else if (c == '<') {
            out += "&lt;";
        } else if (c == '"') {
            out += "&quot;";
        } else {
            out += c;
        }
    }
    return out;
}

bool classHas(const std::string& classAttr, const std::string& cls) {
    size_t i = 0;
    while (i < classAttr.size()) {
        while (i < classAttr.size() && std::isspace((unsigned char)classAttr[i])) {
            ++i;
        }
        size_t j = i;
        while (j < classAttr.size() && !std::isspace((unsigned char)classAttr[j])) {
            ++j;
        }
        if (j > i && classAttr.substr(i, j - i) == cls) {
            return true;
        }
        i = j;
    }
    return false;
}

// Convert shared tree to DOM tree.
std::shared_ptr<SvgElement> cloneNode(const std::shared_ptr<svg::XmlNode>& n) {
    auto e = std::make_shared<SvgElement>();
    e->tag = n->tag;
    e->attrs = n->attrs;
    e->text = n->text;
    for (const auto& c : n->children) {
        auto d = cloneNode(c);
        d->parent = e.get();
        e->children.push_back(d);
    }
    return e;
}

// Collect <style> text into rules.
void collectStyles(const std::shared_ptr<SvgElement>& e,
                   std::vector<CssRule>& rules) {
    if (e->tag == "style" && !e->text.empty()) {
        for (auto& r : svg::parseStylesheet(e->text)) {
            rules.push_back(CssRule{r.selector, r.decls});
        }
    }
    for (const auto& c : e->children) {
        collectStyles(c, rules);
    }
}

}  // namespace

bool CssRule::matches(const SvgElement& el) const {
    if (selector.empty() || selector == "*") {
        return true;
    }
    if (selector[0] == '#') {
        auto id = el.get("id");
        return id && *id == selector.substr(1);
    }
    if (selector[0] == '.') {
        return classHas(el.get("class") ? *el.get("class") : "",
                        selector.substr(1));
    }
    auto dot = selector.find('.');
    if (dot != std::string::npos) {
        if (el.tag != selector.substr(0, dot)) {
            return false;
        }
        return classHas(el.get("class") ? *el.get("class") : "",
                        selector.substr(dot + 1));
    }
    return el.tag == selector;
}

void SvgDocument::reindex() {
    byId.clear();
    if (!root) {
        return;
    }
    std::vector<SvgElement*> stack{root.get()};
    while (!stack.empty()) {
        SvgElement* el = stack.back();
        stack.pop_back();
        if (auto id = el->get("id"); id && !id->empty()) {
            byId[*id] = el;
        }
        for (auto& c : el->children) {
            stack.push_back(c.get());
        }
    }
}

SvgElement* SvgDocument::findId(const std::string& id) const {
    auto it = byId.find(id);
    return it == byId.end() ? nullptr : it->second;
}

std::optional<std::string> SvgDocument::resolved(const SvgElement& el,
                                                 const std::string& prop) const {
    std::optional<std::string> attr;
    if (auto a = el.get(prop); a && prop != "style") {
        attr = *a;
    }
    std::optional<std::string> inlineV;
    if (auto s = el.get("style")) {
        auto m = parseStyleAttr(*s);
        auto it = m.find(prop);
        if (it != m.end()) {
            inlineV = it->second;
        }
    }
    std::optional<std::string> sheetV;
    for (const auto& rule : stylesheet) {
        if (!rule.matches(el)) {
            continue;
        }
        auto it = rule.decls.find(prop);
        if (it != rule.decls.end()) {
            sheetV = it->second;
        }
    }
    if (sheetV) {
        return sheetV;
    }
    if (inlineV) {
        return inlineV;
    }
    return attr;
}

std::map<std::string, std::string> parseStyleAttr(const std::string& style) {
    return svg::parseStyleDecls(style);
}

std::vector<CssRule> parseStylesheet(const std::string& css) {
    std::vector<CssRule> out;
    for (auto& r : svg::parseStylesheet(css)) {
        out.push_back(CssRule{r.selector, r.decls});
    }
    return out;
}

std::string urlRefTarget(const std::string& value) {
    return svg::refTarget(value);
}

std::string normalizeIri(const std::string& value) {
    return svg::normalizeIri(value);
}

SvgDomParse parseSvgDom(const std::string& xml) {
    SvgDomParse out;
    const svg::XmlRead r = svg::readXml(xml);
    if (!r.ok || !r.root) {
        out.error = r.error;
        return out;
    }
    out.doc.root = cloneNode(r.root);
    collectStyles(out.doc.root, out.doc.stylesheet);
    out.doc.reindex();
    out.ok = out.doc.root != nullptr;
    return out;
}

std::string serializeSvgDom(const SvgDocument& doc) {
    if (!doc.root) {
        return "";
    }
    std::string out = "<svg";
    std::function<void(const std::shared_ptr<SvgElement>&, int)> emit =
        [&](const std::shared_ptr<SvgElement>& el, int depth) {
            std::string pad((size_t)(depth * 2), ' ');
            if (el->tag == "style" && el->children.empty()) {
                out += pad + "<style>";
                out += el->text;
                out += "</style>\n";
                return;
            }
            out += pad + "<" + el->tag;
            for (const auto& [k, v] : el->attrs) {
                out += " " + k + "=\"" + encodeAttr(v) + "\"";
            }
            if (el->children.empty() && el->text.empty()) {
                out += "/>\n";
                return;
            }
            out += ">";
            if (!el->children.empty()) {
                out += "\n";
                for (auto& c : el->children) {
                    emit(c, depth + 1);
                }
                out += pad;
            } else if (!el->text.empty()) {
                out += encodeAttr(el->text);
            }
            out += "</" + el->tag + ">\n";
        };
    for (const auto& [k, v] : doc.root->attrs) {
        out += " " + k + "=\"" + encodeAttr(v) + "\"";
    }
    out += ">\n";
    for (auto& c : doc.root->children) {
        emit(c, 1);
    }
    if (!doc.root->text.empty()) {
        out += encodeAttr(doc.root->text);
    }
    out += "</svg>\n";
    return out;
}

}  // namespace pittore::vector
