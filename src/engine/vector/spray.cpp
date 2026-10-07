#include "engine/vector/spray.h"

#include <cmath>

namespace pittore::vector {
namespace {
std::uint32_t lcg(std::uint32_t& s) {
    s = s * 1664525u + 1013904223u;
    return s;
}
double rnd(std::uint32_t& s) { return (lcg(s) >> 8) / 16777216.0; }
}  // namespace

std::vector<SprayStamp> sprayStamps(double cx, double cy, int n, const SpraySpec& spec) {
    std::vector<SprayStamp> out;
    std::uint32_t s = spec.seed;
    for (int i = 0; i < n; i++) {
        double a = rnd(s) * 2 * 3.14159265358979;
        double r = std::sqrt(rnd(s)) * spec.radius * spec.scatter;
        SprayStamp st;
        st.x = cx + r * std::cos(a);
        st.y = cy + r * std::sin(a);
        st.scale = spec.scaleMin + rnd(s) * (spec.scaleMax - spec.scaleMin);
        st.rotationDeg = (rnd(s) * 2 - 1) * spec.rotationJitter;
        st.opacity = 0.4 + 0.6 * rnd(s) * spec.pressureGain;
        if (st.opacity > 1) st.opacity = 1;
        out.push_back(st);
    }
    return out;
}

std::vector<std::pair<double, double>> tweakDisplace(
    const std::vector<std::pair<double, double>>& pts, double cx, double cy,
    double radius, double amount, bool shrink) {
    std::vector<std::pair<double, double>> out;
    for (auto [x, y] : pts) {
        double dx = x - cx, dy = y - cy;
        double d = std::hypot(dx, dy);
        if (d >= radius || d < 1e-9) {
            out.emplace_back(0, 0);
            continue;
        }
        double fall = 0.5 + 0.5 * std::cos(d / radius * 3.14159265358979);
        double k = amount * fall * (shrink ? -1 : 1);
        out.emplace_back(dx / d * k, dy / d * k);
    }
    return out;
}

}  // namespace pittore::vector
