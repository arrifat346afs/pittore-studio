#pragma once
// Gradient stops. Linear sample in place.
#include <vector>

namespace pittore::svg {

struct GradStop {
    double offset = 0;
    float r = 0, g = 0, b = 0, a = 1;
};

struct Gradient {
    bool radial = false;
    double x1 = 0, y1 = 0, x2 = 1, y2 = 0;
    double cx = 0.5, cy = 0.5, r = 0.5;
    std::vector<GradStop> stops;
};

// Clamped linear sample at t.
void sampleStops(const std::vector<GradStop>& stops, double t, float out[4]);

}  // namespace pittore::svg
