#pragma once
// Spray + tweak + scatter: procedural stamping dynamics.
//
// The spray spec covers scatter/rotation/scale/pressure/clone modes and the
// tweak spec push/shrink/grow/roughen/colour. The engine produces
// deterministic stamp transforms from a seeded RNG so strokes replay
// identically; the canvas previews the outline before committing.
#include <cstdint>
#include <vector>

namespace pittore::vector {

struct SpraySpec {
    double radius = 40.0;     // spray cone radius
    double scatter = 1.0;     // 0..1 positional jitter
    double scaleMin = 0.5, scaleMax = 1.5;
    double rotationJitter = 180.0;  // degrees
    double pressureGain = 1.0;
    std::uint32_t seed = 12345;
};

struct SprayStamp {
    double x = 0.0, y = 0.0;
    double scale = 1.0;
    double rotationDeg = 0.0;
    double opacity = 1.0;
};

// N stamps inside the spray cone at (cx,cy). Deterministic in `spec.seed`.
std::vector<SprayStamp> sprayStamps(double cx, double cy, int n, const SpraySpec& spec);

// Tweak brush: displace/scale points inside the brush radius toward/away from
// the brush center (push/shrink/grow) with falloff. Returns translations.
std::vector<std::pair<double, double>> tweakDisplace(
    const std::vector<std::pair<double, double>>& pts, double cx, double cy,
    double radius, double amount, bool shrink = false);

}  // namespace pittore::vector
