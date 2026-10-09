// Length parse. One unit table, no caller inline math.
#include "engine/vector/svg/length.h"

#include <cctype>
#include <cstdlib>

namespace pittore::svg {
namespace {

// Unit suffix match.
LengthUnit matchUnit(std::string_view u) {
    if (u.empty()) {
        return LengthUnit::None;
    }
    if (u == "px") {
        return LengthUnit::Px;
    }
    if (u == "pt") {
        return LengthUnit::Pt;
    }
    if (u == "pc") {
        return LengthUnit::Pc;
    }
    if (u == "mm") {
        return LengthUnit::Mm;
    }
    if (u == "cm") {
        return LengthUnit::Cm;
    }
    if (u == "in") {
        return LengthUnit::In;
    }
    if (u == "em") {
        return LengthUnit::Em;
    }
    if (u == "ex") {
        return LengthUnit::Ex;
    }
    if (u == "%") {
        return LengthUnit::Percent;
    }
    return LengthUnit::None;
}

}  // namespace

Length parseLength(std::string_view s) {
    Length out;
    size_t a = 0;
    while (a < s.size() && std::isspace((unsigned char)s[a])) {
        ++a;
    }
    size_t b = s.size();
    while (b > a && std::isspace((unsigned char)s[b - 1])) {
        --b;
    }
    if (a >= b) {
        return out;
    }
    const std::string t(s.substr(a, b - a));
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (end == t.c_str()) {
        return out;
    }
    std::string_view u;
    if (end != nullptr) {
        const size_t off = (size_t)(end - t.c_str());
        u = std::string_view(t).substr(off);
        // Trim unit blanks.
        while (!u.empty() && std::isspace((unsigned char)u.front())) {
            u.remove_prefix(1);
        }
        while (!u.empty() && std::isspace((unsigned char)u.back())) {
            u.remove_suffix(1);
        }
    }
    // Unknown suffix is invalid, not silently px.
    if (!u.empty() && u != "%" && u != "px" && u != "pt" && u != "pc" &&
        u != "mm" && u != "cm" && u != "in" && u != "em" && u != "ex") {
        return out;
    }
    out.value = v;
    out.unit = matchUnit(u);
    out.valid = true;
    return out;
}

double toPx(Length l, double ref, double emPx, double exPx) {
    if (!l.valid) {
        return 0.0;
    }
    switch (l.unit) {
        case LengthUnit::None:
        case LengthUnit::Px:
            return l.value;
        case LengthUnit::Pt:
            return l.value * 96.0 / 72.0;
        case LengthUnit::Pc:
            return l.value * 16.0;
        case LengthUnit::Mm:
            return l.value * 96.0 / 25.4;
        case LengthUnit::Cm:
            return l.value * 96.0 / 2.54;
        case LengthUnit::In:
            return l.value * 96.0;
        case LengthUnit::Em:
            return l.value * emPx;
        case LengthUnit::Ex:
            return l.value * exPx;
        case LengthUnit::Percent:
            return l.value / 100.0 * ref;
    }
    return 0.0;
}

double readLength(const std::string& s, double ref, double fallback) {
    const Length l = parseLength(s);
    if (!l.valid) {
        return fallback;
    }
    return toPx(l, ref);
}

}  // namespace pittore::svg
