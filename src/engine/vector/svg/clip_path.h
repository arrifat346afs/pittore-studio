#pragma once
// Clip test. Union of rings, per-ring rule.
#include <utility>
#include <vector>

namespace pittore::svg {

struct ClipPoly {
    std::vector<std::pair<double, double>> pts;
    bool evenOdd = false;
};

bool pointInPoly(const std::vector<std::pair<double, double>>& pts, double x,
                 double y, bool evenOdd);
bool pointInClip(const std::vector<ClipPoly>& clip, double x, double y);

}  // namespace pittore::svg
