// Dash parse. Negatives clamp to 0.
#include "engine/vector/svg/dash.h"

#include "engine/vector/svg/number_list.h"

namespace pittore::svg {

std::vector<double> parseDashArray(const std::string& s) {
    if (s.empty() || s == "none") {
        return {};
    }
    std::vector<double> out = parseDoubles(s);
    for (double& v : out) {
        if (v < 0) {
            v = 0;
        }
    }
    // Odd count repeats to even per spec.
    if (out.size() % 2 == 1) {
        const size_t n = out.size();
        for (size_t k = 0; k < n; ++k) {
            out.push_back(out[k]);
        }
    }
    return out;
}

double parseDashOffset(const std::string& s) {
    const auto v = parseDoubles(s);
    return v.empty() ? 0 : v[0];
}

}  // namespace pittore::svg
