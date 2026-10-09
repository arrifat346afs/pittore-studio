#pragma once
// Text box estimate. Matches importer box math.
#include <string>

namespace pittore::svg {

struct TextBox {
    double x = 0, y = 0, w = 0, h = 0;
};

TextBox measureText(const std::string& body, double fontSize, double bx,
                    double by, const std::string& anchor,
                    const std::string& baseline);

}  // namespace pittore::svg
