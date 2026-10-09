#pragma once
// Marker frames along a polyline.
#include <utility>
#include <vector>

namespace pittore::svg {

struct MarkerFrame {
    double x = 0, y = 0, angleDeg = 0;
};

std::vector<MarkerFrame> framesForPolyline(
    const std::vector<std::pair<double, double>>& pts, bool closed);

}  // namespace pittore::svg
