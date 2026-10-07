// SVG DOM: tiny XML subset parser + cascade resolver. The tree keeps its own
// representation, an id index and a style cascade. Only <svg> vocabulary is
// needed, so entities/comments/PIs are skipped rather than modelled.
#include "engine/vector/svg_dom.h"

#include <cctype>
#include <functional>

namespace pittore::vector {
namespace {

bool isNameChar(char c) {
    return std::isalnum((unsigned char)c || c == '.' || c == '-' || c == '_' ||
                        c == ':' || c == '#');
}

void skipWs(const std::string& s, size_t& i) {
    while (i < s.size() && std::isspace((unsigned char)s[i])) i++;
}

// Decode the 5 predefined entities + numeric refs; unknown entities pass
// through so the serializer round-trips them.
std::string decodeEntities(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size();) {
        if (in[i] != '&') {
            out += in[i++];
            continue;
        }
        size_t semi = in.find(';', i);
        if (semi == std::string::npos) {
            out += in[i++];
            continue;
        }
        std::string ent = in.substr(i + 1, semi - i - 1);
        if (ent == "amp")
            out += '&';
        else if (ent == "lt")
            out += '<';
        else if (ent == "gt")
            out += '>';
        else if (ent == "quot")
            out += '"';
        else if (ent == "apos")
            out += '\'';
        else if (!ent.empty() && ent[0] == '#') {
            unsigned v = 0;
            try {
                v = (unsigned)std::stoul(
                    ent[1] == 'x' || ent[1] == 'X' ? ent.substr(2) : ent.substr(1),
                    nullptr, ent[1] == 'x' || ent[1] == 'X' ? 16 : 10);
            } catch (...) {
                v = 0;
            }
            if (v < 0x80)
                out += (char)v;
            else if (v < 0x800) {
                out += (char)(0xC0 | (v >> 6));
                out += (char)(0x80 | (v & 0x3F));
            } else {
                out += (char)(0xE0 | (v >> 12));
                out += (char)(0x80 | ((v >> 6) & 0x3F));
                out += (char)(0x80 | (v & 0x3F));
            }
        } else {
            out += in.substr(i, semi - i + 1);
        }
        i = semi + 1;
    }
    return out;
}

std::string encodeAttr(const std::string& in) {
    std::string out;
    for (char c : in) {
        if (c == '&')
            out += "&amp;";
        else if (c == '<')
            out += "&lt;";
        else if (c == '"')
            out += "&quot;";
        else
            out += c;
    }
    return out;
}

bool classHas(const std::string& classAttr, const std::string& cls) {
    size_t i = 0;
    while (i < classAttr.size()) {
        while (i < classAttr.size() && std::isspace((unsigned char)classAttr[i])) i++;
        size_t j = i;
        while (j < classAttr.size() && !std::isspace((unsigned char)classAttr[j])) j++;
        if (j > i && classAttr.substr(i, j - i) == cls) return true;
        i = j;
    }
    return false;
}

}  // namespace

bool CssRule::matches(const SvgElement& el) const {
    if (selector.empty() || selector == "*") return true;
    if (selector[0] == '#') {
        auto id = el.get("id");
        return id && *id == selector.substr(1);
    }
    if (selector[0] == '.') return classHas(el.get("class") ? *el.get("class") : "",
                                            selector.substr(1));
    auto dot = selector.find('.');
    if (dot != std::string::npos) {
        if (el.tag != selector.substr(0, dot)) return false;
        return classHas(el.get("class") ? *el.get("class") : "",
                        selector.substr(dot + 1));
    }
    return el.tag == selector;
}

void SvgDocument::reindex() {
    byId.clear();
    if (!root) return;
    std::vector<SvgElement*> stack{root.get()};
    while (!stack.empty()) {
        SvgElement* el = stack.back();
        stack.pop_back();
        if (auto id = el->get("id"); id && !id->empty()) byId[*id] = el;
        for (auto& c : el->children) stack.push_back(c.get());
    }
}

SvgElement* SvgDocument::findId(const std::string& id) const {
    auto it = byId.find(id);
    return it == byId.end() ? nullptr : it->second;
}

std::optional<std::string> SvgDocument::resolved(const SvgElement& el,
                                                 const std::string& prop) const {
    std::optional<std::string> out;
    // Stylesheet first (later rules win), then style="", then attribute.
    for (const auto& rule : stylesheet) {
        if (!rule.matches(el)) continue;
        auto it = rule.decls.find(prop);
        if (it != rule.decls.end()) out = it->second;
    }
    if (auto s = el.get("style")) {
        for (const auto& [k, v] : parseStyleAttr(*s))
            if (k == prop) out = v;
    }
    if (auto a = el.get(prop)) out = *a;
    // Stylesheet/style beat the attribute, so re-apply them on top.
    // (Attribute set `out` last above; correct order is attr < style < sheet.)
    // Re-resolve in the right precedence:
    std::optional<std::string> attr;
    if (auto a = el.get(prop); a && prop != "style") attr = *a;
    std::optional<std::string> inlineV;
    if (auto s = el.get("style")) {
        auto m = parseStyleAttr(*s);
        auto it = m.find(prop);
        if (it != m.end()) inlineV = it->second;
    }
    std::optional<std::string> sheetV;
    for (const auto& rule : stylesheet) {
        if (!rule.matches(el)) continue;
        auto it = rule.decls.find(prop);
        if (it != rule.decls.end()) sheetV = it->second;
    }
    if (sheetV) return sheetV;
    if (inlineV) return inlineV;
    return attr;
}

std::map<std::string, std::string> parseStyleAttr(const std::string& style) {
    std::map<std::string, std::string> out;
    size_t i = 0;
    while (i < style.size()) {
        while (i < style.size() &&
               (std::isspace((unsigned char)style[i]) || style[i] == ';'))
            i++;
        size_t k0 = i;
        while (i < style.size() && style[i] != ':' && style[i] != ';') i++;
        if (i >= style.size() || style[i] != ':') continue;
        std::string key = style.substr(k0, i - k0);
        i++;
        size_t v0 = i;
        while (i < style.size() && style[i] != ';') i++;
        std::string val = style.substr(v0, i - v0);
        auto trim = [](std::string s) {
            size_t a = 0;
            while (a < s.size() && std::isspace((unsigned char)s[a])) a++;
            size_t b = s.size();
            while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
            return s.substr(a, b - a);
        };
        key = trim(key);
        val = trim(val);
        if (!key.empty()) out[key] = val;
    }
    return out;
}

std::vector<CssRule> parseStylesheet(const std::string& css) {
    std::vector<CssRule> rules;
    size_t i = 0;
    while (i < css.size()) {
        // Skip comments.
        if (css.compare(i, 2, "/*") == 0) {
            size_t end = css.find("*/", i + 2);
            i = end == std::string::npos ? css.size() : end + 2;
            continue;
        }
        if (std::isspace((unsigned char)css[i])) {
            i++;
            continue;
        }
        size_t bOpen = css.find('{', i);
        if (bOpen == std::string::npos) break;
        std::string sel = css.substr(i, bOpen - i);
        size_t bClose = css.find('}', bOpen);
        if (bClose == std::string::npos) break;
        // Multiple selectors split on ','.
        size_t s0 = 0;
        auto trim = [](const std::string& s) {
            size_t a = 0;
            while (a < s.size() && std::isspace((unsigned char)s[a])) a++;
            size_t b = s.size();
            while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
            return s.substr(a, b - a);
        };
        std::string body = css.substr(bOpen + 1, bClose - bOpen - 1);
        auto decls = parseStyleAttr(body);
        while (s0 < sel.size()) {
            size_t comma = sel.find(',', s0);
            std::string one =
                trim(sel.substr(s0, comma == std::string::npos ? std::string::npos
                                                              : comma - s0));
            if (!one.empty()) rules.push_back(CssRule{one, decls});
            if (comma == std::string::npos) break;
            s0 = comma + 1;
        }
        i = bClose + 1;
    }
    return rules;
}

std::string urlRefTarget(const std::string& value) {
    size_t h = value.find('#');
    if (value.compare(0, 4, "url(") != 0 || h == std::string::npos) return "";
    size_t end = value.find(')', h);
    std::string id = value.substr(h + 1, end == std::string::npos ? std::string::npos
                                                                 : end - h - 1);
    return normalizeIri(id);
}

std::string normalizeIri(const std::string& value) {
    size_t a = 0;
    while (a < value.size() &&
           (std::isspace((unsigned char)value[a]) || value[a] == '#' ||
            value[a] == '"' || value[a] == '\'')) {
        if (value[a] == '#') {
            a++;
            break;
        }
        a++;
    }
    // Handle "#id" directly.
    if (!value.empty() && value[0] == '#') a = 1;
    size_t b = value.size();
    while (b > a && (std::isspace((unsigned char)value[b - 1]) || value[b - 1] == '"' ||
                     value[b - 1] == '\'' || value[b - 1] == ')'))
        b--;
    return value.substr(a, b > a ? b - a : 0);
}

SvgDomParse parseSvgDom(const std::string& xml) {
    SvgDomParse out;
    out.doc.root = nullptr;
    std::vector<std::shared_ptr<SvgElement>> stack;
    size_t i = 0;
    auto appendText = [&](const std::string& t) {
        if (stack.empty() || t.empty()) return;
        // Keep only non-blank text (element whitespace is not content).
        bool blank = true;
        for (char c : t)
            if (!std::isspace((unsigned char)c)) {
                blank = false;
                break;
            }
        if (!blank) stack.back()->text += decodeEntities(t);
    };
    while (i < xml.size()) {
        if (xml[i] != '<') {
            size_t lt = xml.find('<', i);
            appendText(xml.substr(i, lt == std::string::npos ? std::string::npos
                                                            : lt - i));
            i = lt == std::string::npos ? xml.size() : lt;
            continue;
        }
        // Comments, PIs, doctype: skip.
        if (xml.compare(i, 4, "<!--") == 0) {
            size_t end = xml.find("-->", i + 4);
            if (end == std::string::npos) {
                out.error = "unterminated comment";
                return out;
            }
            i = end + 3;
            continue;
        }
        if (xml.compare(i, 2, "<?") == 0) {
            size_t end = xml.find("?>", i + 2);
            i = end == std::string::npos ? xml.size() : end + 2;
            continue;
        }
        if (xml.compare(i, 9, "<!DOCTYPE") == 0) {
            size_t end = xml.find('>', i + 9);
            i = end == std::string::npos ? xml.size() : end + 1;
            continue;
        }
        if (xml.compare(i, 9, "<![CDATA[") == 0) {
            size_t end = xml.find("]]>", i + 9);
            std::string data = xml.substr(i + 9, end == std::string::npos
                                                     ? std::string::npos
                                                     : end - i - 9);
            appendText(data);
            i = end == std::string::npos ? xml.size() : end + 3;
            continue;
        }
        bool close = i + 1 < xml.size() && xml[i + 1] == '/';
        size_t j = i + (close ? 2 : 1);
        skipWs(xml, j);
        size_t n0 = j;
        while (j < xml.size() && (std::isalnum((unsigned char)xml[j]) || xml[j] == '-' ||
                                 xml[j] == '_' || xml[j] == ':' || xml[j] == '.'))
            j++;
        std::string tag = xml.substr(n0, j - n0);
        if (tag.empty()) {
            out.error = "bad tag";
            return out;
        }
        if (close) {
            size_t gt = xml.find('>', j);
            if (gt == std::string::npos) {
                out.error = "unterminated close tag";
                return out;
            }
            if (!stack.empty() && stack.back()->tag == tag) {
                auto done = stack.back();
                stack.pop_back();
                if (stack.empty())
                    out.doc.root = done;
                else {
                    done->parent = stack.back().get();
                    stack.back()->children.push_back(done);
                }
            }
            i = gt + 1;
            continue;
        }
        auto el = std::make_shared<SvgElement>();
        el->tag = tag;
        bool selfClose = false;
        while (j < xml.size()) {
            skipWs(xml, j);
            if (j >= xml.size()) break;
            if (xml[j] == '>') {
                j++;
                break;
            }
            if (xml.compare(j, 2, "/>") == 0) {
                selfClose = true;
                j += 2;
                break;
            }
            size_t a0 = j;
            while (j < xml.size() && (std::isalnum((unsigned char)xml[j]) || xml[j] == '-' ||
                                     xml[j] == '_' || xml[j] == ':' || xml[j] == '.'))
                j++;
            std::string attr = xml.substr(a0, j - a0);
            skipWs(xml, j);
            std::string val;
            if (j < xml.size() && xml[j] == '=') {
                j++;
                skipWs(xml, j);
                if (j < xml.size() && (xml[j] == '"' || xml[j] == '\'')) {
                    char q = xml[j++];
                    size_t v0 = j;
                    size_t ve = xml.find(q, j);
                    if (ve == std::string::npos) {
                        out.error = "unterminated attribute";
                        return out;
                    }
                    val = decodeEntities(xml.substr(v0, ve - v0));
                    j = ve + 1;
                } else {
                    size_t v0 = j;
                    while (j < xml.size() && !std::isspace((unsigned char)xml[j]) &&
                           xml[j] != '>' && xml[j] != '/')
                        j++;
                    val = xml.substr(v0, j - v0);
                }
            }
            if (!attr.empty()) el->attrs[attr] = val;
        }
        if (el->tag == "style" && !selfClose) {
            // Collect raw CSS until </style>.
            size_t end = xml.find("</style", j);
            std::string css = xml.substr(j, end == std::string::npos ? std::string::npos
                                                                     : end - j);
            auto rules = parseStylesheet(css);
            out.doc.stylesheet.insert(out.doc.stylesheet.end(), rules.begin(),
                                      rules.end());
            // Keep the node too so the editor round-trips it.
            if (end == std::string::npos) {
                j = xml.size();
            } else {
                size_t gt = xml.find('>', end);
                j = gt == std::string::npos ? xml.size() : gt + 1;
            }
            el->text = css;
            if (stack.empty())
                out.doc.root = el;
            else {
                el->parent = stack.back().get();
                stack.back()->children.push_back(el);
            }
            i = j;
            continue;
        }
        if (selfClose) {
            if (stack.empty() && !out.doc.root)
                out.doc.root = el;
            else if (!stack.empty()) {
                el->parent = stack.back().get();
                stack.back()->children.push_back(el);
            } else if (out.doc.root) {
                // Trailing sibling: wrap in a group so nothing is lost.
                auto wrap = std::make_shared<SvgElement>();
                wrap->tag = "svg";
                out.doc.root->parent = wrap.get();
                wrap->children.push_back(out.doc.root);
                el->parent = wrap.get();
                wrap->children.push_back(el);
                out.doc.root = wrap;
            }
            i = j;
            continue;
        }
        stack.push_back(el);
        i = j;
    }
    // Unclosed elements: attach what we have.
    while (!stack.empty()) {
        auto done = stack.back();
        stack.pop_back();
        if (stack.empty()) {
            if (!out.doc.root) out.doc.root = done;
        } else {
            done->parent = stack.back().get();
            stack.back()->children.push_back(done);
        }
    }
    out.doc.reindex();
    out.ok = out.doc.root != nullptr;
    return out;
}

std::string serializeSvgDom(const SvgDocument& doc) {
    if (!doc.root) return "";
    std::string out = "<svg";
    // Hoist namespace-less root attrs first in stable (sorted) order; std::map
    // already iterates sorted, which keeps output deterministic.
    std::string inner;
    std::function<void(const std::shared_ptr<SvgElement>&, int)> emit =
        [&](const std::shared_ptr<SvgElement>& el, int depth) {
            std::string pad((size_t)(depth * 2), ' ');
            // <style> re-emits its CSS verbatim.
            if (el->tag == "style" && el->children.empty()) {
                out += pad + "<style>";
                out += el->text;
                out += "</style>\n";
                return;
            }
            out += pad + "<" + el->tag;
            for (const auto& [k, v] : el->attrs) out += " " + k + "=\"" + encodeAttr(v) + "\"";
            if (el->children.empty() && el->text.empty()) {
                out += "/>\n";
                return;
            }
            out += ">";
            if (!el->children.empty()) {
                out += "\n";
                for (auto& c : el->children) emit(c, depth + 1);
                out += pad;
            } else if (!el->text.empty()) {
                out += encodeAttr(el->text);
            }
            out += "</" + el->tag + ">\n";
        };
    // Emit root attrs on the <svg> line then children.
    for (const auto& [k, v] : doc.root->attrs) out += " " + k + "=\"" + encodeAttr(v) + "\"";
    out += ">\n";
    for (auto& c : doc.root->children) emit(c, 1);
    if (!doc.root->text.empty()) out += encodeAttr(doc.root->text);
    out += "</svg>\n";
    (void)inner;
    return out;
}

}  // namespace pittore::vector
