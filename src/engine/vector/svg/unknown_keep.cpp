// Core attr sets per tag.
#include "engine/vector/svg/unknown_keep.h"

namespace pittore::svg {
namespace {

bool in(const std::string& v, const char* const* list, int n) {
    for (int k = 0; k < n; ++k) {
        if (v == list[k]) {
            return true;
        }
    }
    return false;
}

}  // namespace

bool isKnownAttr(const std::string& tag, const std::string& attr) {
    static const char* core[] = {"id", "class", "style", "transform",
                                 "opacity", "display", "visibility"};
    if (in(attr, core, 7)) {
        return true;
    }
    if (tag == "rect") {
        static const char* k[] = {"x", "y", "width", "height", "rx", "ry",
                                  "fill", "stroke", "stroke-width"};
        return in(attr, k, 9);
    }
    if (tag == "circle") {
        static const char* k[] = {"cx", "cy", "r", "fill", "stroke",
                                  "stroke-width"};
        return in(attr, k, 6);
    }
    if (tag == "path") {
        static const char* k[] = {"d", "fill", "stroke", "stroke-width",
                                  "fill-rule"};
        return in(attr, k, 5);
    }
    return attr == "fill" || attr == "stroke";
}

std::map<std::string, std::string> unknownAttrs(
    const std::string& tag, const std::map<std::string, std::string>& attrs) {
    std::map<std::string, std::string> out;
    for (const auto& [k, v] : attrs) {
        if (!isKnownAttr(tag, k)) {
            out.emplace(k, v);
        }
    }
    return out;
}

}  // namespace pittore::svg
