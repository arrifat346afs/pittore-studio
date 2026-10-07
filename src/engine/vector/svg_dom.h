#pragma once
// Lightweight SVG DOM: element tree with id index and attribute cascade.
//
// A toolkit-free tree the importer builds, <use>/clones resolve against, the
// XML editor shows, and extensions mutate.
// Presentation resolution order: presentation attribute < style="" <
// stylesheet rules (matched by #id/.class/tag, later wins). Only the
// properties the vector engine understands are resolved; the rest round-trip
// untouched in `attrs` so save keeps unknown data.
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace pittore::vector {

// One element. Text nodes fold into `text` of their parent.
struct SvgElement {
    std::string tag;  // "svg", "g", "path", "use", ...
    std::map<std::string, std::string> attrs;  // insertion-ordered for stable emit
    std::vector<std::shared_ptr<SvgElement>> children;
    std::string text;
    SvgElement* parent = nullptr;

    const std::string* get(const std::string& name) const {
        auto it = attrs.find(name);
        return it == attrs.end() ? nullptr : &it->second;
    }
    void set(const std::string& name, const std::string& value) {
        attrs[name] = value;
    }
    bool del(const std::string& name) { return attrs.erase(name) > 0; }
};

// One `selector { prop: value; ... }` rule, in document order.
struct CssRule {
    std::string selector;  // "#id", ".class", "tag", "tag.class", "*"
    std::map<std::string, std::string> decls;
    bool matches(const SvgElement& el) const;
};

// Whole document: root + id index + stylesheet rules.
struct SvgDocument {
    std::shared_ptr<SvgElement> root;
    std::unordered_map<std::string, SvgElement*> byId;
    std::vector<CssRule> stylesheet;

    void reindex();
    SvgElement* findId(const std::string& id) const;
    // Resolved value for `prop`: stylesheet (later wins) > style="" > attribute.
    std::optional<std::string> resolved(const SvgElement& el,
                                        const std::string& prop) const;
};

// Parse a whole SVG document. Never throws: malformed input yields the
// prefix parsed so far with `ok=false`.
struct SvgDomParse {
    SvgDocument doc;
    bool ok = false;
    std::string error;
};
SvgDomParse parseSvgDom(const std::string& xml);

// Serialize back to standalone SVG text (2-space indent, `<?xml?>` omitted).
std::string serializeSvgDom(const SvgDocument& doc);

// Stylesheet helpers shared by the importer and the XML editor.
std::vector<CssRule> parseStylesheet(const std::string& css);
std::map<std::string, std::string> parseStyleAttr(const std::string& style);

// url(#id) target, or empty when `value` is not a reference.
std::string urlRefTarget(const std::string& value);
// "#id", "url(#id)" and "#id"-with-whitespace all normalize to "id".
std::string normalizeIri(const std::string& value);

}  // namespace pittore::vector
