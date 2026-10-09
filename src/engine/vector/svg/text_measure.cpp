// Box math matches flat importer boxes.
#include "engine/vector/svg/text_measure.h"

namespace pittore::svg {

TextBox measureText(const std::string& body, double fontSize, double bx,
                    double by, const std::string& anchor,
                    const std::string& baseline) {
    int n = 0;
    for (char c : body) {
        if (c != ' ' && c != '\t' && c != '\n') {
            ++n;
        }
    }
    const double w = n * 0.62 * fontSize;
    const double h = 1.3 * fontSize;
    double x = bx;
    if (anchor == "middle") {
        x = bx - w / 2;
    } else if (anchor == "end") {
        x = bx - w;
    }
    double y = by - h;
    if (baseline == "middle") {
        y = by - h / 2;
    } else if (baseline == "hanging") {
        y = by;
    }
    return TextBox{x, y, w, h};
}

}  // namespace pittore::svg
