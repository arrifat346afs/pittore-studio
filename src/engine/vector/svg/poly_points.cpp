// Pairs of numbers.
#include "engine/vector/svg/poly_points.h"

#include "engine/vector/svg/number_list.h"

namespace pittore::svg {

std::vector<std::pair<double, double>> parsePolyPoints(const std::string& s) {
    const auto v = parseDoubles(s);
    std::vector<std::pair<double, double>> out;
    for (size_t k = 0; k + 1 < v.size(); k += 2) {
        out.emplace_back(v[k], v[k + 1]);
    }
    return out;
}

}  // namespace pittore::svg
