#pragma once
// Parameter-vector accessors shared by every apply_* file and the registry.
// Split from engine/filter/filters.h.
#include <cstddef>
#include <vector>

namespace pittore::filter {

inline double pv(const std::vector<double> &v, std::size_t i, double d) {
    return i < v.size() ? v[i] : d;
}

inline double p2v(const std::vector<double> &v) {
    return v.size() > 2 ? v[2] : 0.0;
}

inline double p3v(const std::vector<double> &v) {
    return v.size() > 3 ? v[3] : 0.0;
}

inline double p4v(const std::vector<double> &v) {
    return v.size() > 4 ? v[4] : 0.0;
}

inline double p5v(const std::vector<double> &v) {
    return v.size() > 5 ? v[5] : 0.0;
}

inline double p6v(const std::vector<double> &v) {
    return v.size() > 6 ? v[6] : 0.0;
}

inline double p7v(const std::vector<double> &v) {
    return v.size() > 7 ? v[7] : 0.0;
}

inline double p8v(const std::vector<double> &v) {
    return v.size() > 8 ? v[8] : 0.0;
}

inline double p9v(const std::vector<double> &v) {
    return v.size() > 9 ? v[9] : 0.0;
}

inline double p10v(const std::vector<double> &v) {
    return v.size() > 10 ? v[10] : 0.0;
}

inline double p11v(const std::vector<double> &v) {
    return v.size() > 11 ? v[11] : 0.0;
}

inline double p12v(const std::vector<double> &v) {
    return v.size() > 12 ? v[12] : 0.0;
}

inline double p13v(const std::vector<double> &v) {
    return v.size() > 13 ? v[13] : 0.0;
}

inline double p14v(const std::vector<double> &v) {
    return v.size() > 14 ? v[14] : 0.0;
}

}  // namespace pittore::filter
