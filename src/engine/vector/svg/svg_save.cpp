// Verbatim emit with 2-space indent.
#include "engine/vector/svg/svg_save.h"

#include <functional>

#include "engine/vector/svg/xml_reader.h"

namespace pittore::svg {
namespace {

std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '&') {
            o += "&amp;";
        } else if (c == '<') {
            o += "&lt;";
        } else if (c == '"') {
            o += "&quot;";
        } else {
            o += c;
        }
    }
    return o;
}

}  // namespace

std::string saveXml(const XmlNode& root) {
    std::string out;
    std::function<void(const XmlNode&, int)> emit = [&](const XmlNode& e,
                                                         int d) {
        std::string pad((size_t)(d * 2), ' ');
        out += pad + "<" + e.tag;
        for (const auto& [k, v] : e.attrs) {
            out += " " + k + "=\"" + esc(v) + "\"";
        }
        if (e.children.empty() && e.text.empty()) {
            out += "/>\n";
            return;
        }
        out += ">";
        if (!e.children.empty()) {
            out += "\n";
            for (const auto& c : e.children) {
                emit(*c, d + 1);
            }
            out += pad;
        } else {
            out += esc(e.text);
        }
        out += "</" + e.tag + ">\n";
    };
    emit(root, 0);
    return out;
}

}  // namespace pittore::svg
