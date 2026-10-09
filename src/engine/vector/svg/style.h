#pragma once
// Resolved style for one node. Inherits from parent.
#include <string>
#include <vector>

#include "engine/vector/svg/color_parse.h"
#include "engine/vector/svg/css_parse.h"

namespace pittore::svg {

struct XmlNode;
struct CssRule;

// Flat resolved values.
struct Style {
    Paint fill;
    Paint stroke;
    double opacity = 1.0;
    double fillOpacity = 1.0;
    double strokeOpacity = 1.0;
    double strokeWidth = 1.0;
    bool displayNone = false;
    std::string clipRef;
    std::string maskRef;
    std::string filterRef;
};

// Cascade: attr, then style="", then sheet. Inherits opacity.
Style resolveStyle(const XmlNode& n, const std::vector<CssRule>& sheet,
                   const Style* parent);

}  // namespace pittore::svg
