#pragma once
// One length string to px. Bad input gives 0.
#include <string>
#include <string_view>

namespace pittore::svg {

enum class LengthUnit { None, Px, Pt, Pc, Mm, Cm, In, Em, Ex, Percent };

struct Length {
    double value = 0;
    LengthUnit unit = LengthUnit::None;
    bool valid = false;
};

Length parseLength(std::string_view s);

// Px value. ref scales %, emPx scales em, exPx scales ex.
double toPx(Length l, double ref, double emPx = 16.0, double exPx = 8.0);

// Direct read with fallback.
double readLength(const std::string& s, double ref, double fallback = 0.0);

}  // namespace pittore::svg
