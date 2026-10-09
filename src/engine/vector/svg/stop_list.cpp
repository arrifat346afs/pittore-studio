// Rows to sorted stops. Bad rows dropped.
#include "engine/vector/svg/stop_list.h"

#include <algorithm>
#include <cstdlib>

#include "engine/vector/svg/color_parse.h"

namespace pittore::svg {

std::vector<GradStop> buildStops(const std::vector<StopRow>& rows) {
    std::vector<GradStop> out;
    for (const auto& r : rows) {
        char* end = nullptr;
        double off = std::strtod(r.offset.c_str(), &end);
        if (end == r.offset.c_str()) {
            continue;
        }
        if (r.offset.find('%') != std::string::npos) {
            off /= 100.0;
        }
        float c[4] = {0, 0, 0, 1};
        if (!r.color.empty() && !parseColor(r.color, c)) {
            continue;
        }
        double a = c[3];
        if (!r.opacity.empty()) {
            char* e2 = nullptr;
            const double v = std::strtod(r.opacity.c_str(), &e2);
            if (e2 != r.opacity.c_str()) {
                a *= v;
            }
        }
        out.push_back(GradStop{off, c[0], c[1], c[2], (float)a});
    }
    std::sort(out.begin(), out.end(), [](const GradStop& a, const GradStop& b) {
        return a.offset < b.offset;
    });
    return out;
}

}  // namespace pittore::svg
