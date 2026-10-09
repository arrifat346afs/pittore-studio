#pragma once
// Stroke props from strings. No DOM here.
#include <string>
#include <vector>

namespace pittore::svg {

enum class Cap { Butt, Round, Square };
enum class Join { Miter, Round, Bevel };

struct StrokeStyle {
    double width = 1.0;
    Cap cap = Cap::Butt;
    Join join = Join::Round;
    double miter = 4.0;
    std::vector<double> dash;
    double dashOffset = 0;
};

Cap parseCap(const std::string& s);
Join parseJoin(const std::string& s);
StrokeStyle makeStroke(double width, const std::string& cap,
                       const std::string& join, const std::string& miter,
                       const std::string& dash, const std::string& offset);

}  // namespace pittore::svg
