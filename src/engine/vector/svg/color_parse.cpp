// Paint parse. Hex, names, rgb(), hsl(). Rest is later work.
#include "engine/vector/svg/color_parse.h"

#include <cctype>
#include <cmath>
#include <cstdlib>

namespace pittore::svg {
namespace {

// sRGB channel to linear.
float toLin(float c) {
    return c <= 0.04045f ? c / 12.92f
                         : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

int hexDig(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

std::string trimStr(std::string_view s) {
    size_t a = 0;
    while (a < s.size() && std::isspace((unsigned char)s[a])) {
        ++a;
    }
    size_t b = s.size();
    while (b > a && std::isspace((unsigned char)s[b - 1])) {
        --b;
    }
    return std::string(s.substr(a, b - a));
}

bool litColor(std::string_view s, float rgba[4]) {
    if (s == "black") {
        rgba[0] = rgba[1] = rgba[2] = 0;
        rgba[3] = 1;
        return true;
    }
    if (s == "white") {
        rgba[0] = rgba[1] = rgba[2] = 1;
        rgba[3] = 1;
        return true;
    }
    if (s == "red") {
        rgba[0] = 1;
        rgba[1] = rgba[2] = 0;
        rgba[3] = 1;
        return true;
    }
    if (s == "green") {
        rgba[1] = 1;
        rgba[0] = rgba[2] = 0;
        rgba[3] = 1;
        return true;
    }
    if (s == "blue") {
        rgba[2] = 1;
        rgba[0] = rgba[1] = 0;
        rgba[3] = 1;
        return true;
    }
    if (s == "gray" || s == "grey") {
        const float g = toLin(0.502f);
        rgba[0] = rgba[1] = rgba[2] = g;
        rgba[3] = 1;
        return true;
    }
    if (s == "none") {
        return false;
    }
    return false;
}

// Split comma or space list to doubles.
int splitNums(std::string_view body, double* out, int max) {
    std::string t(body);
    for (char& c : t) {
        if (c == ',') {
            c = ' ';
        }
    }
    int n = 0;
    size_t i = 0;
    while (i < t.size() && n < max) {
        while (i < t.size() && std::isspace((unsigned char)t[i])) {
            ++i;
        }
        if (i >= t.size()) {
            break;
        }
        // Skip slash alpha separator.
        if (t[i] == '/') {
            ++i;
            continue;
        }
        char* end = nullptr;
        const double v = std::strtod(t.c_str() + i, &end);
        if (end == t.c_str() + i) {
            break;
        }
        // Keep % raw; caller scales.
        bool pct = end[0] == '%';
        out[n++] = pct ? v / 100.0 : v;
        i = (size_t)(end - t.c_str());
        if (pct) {
            ++i;
        }
    }
    return n;
}

// Hue to channel helper.
float hueChan(float p, float q, float t) {
    if (t < 0) {
        t += 1;
    }
    if (t > 1) {
        t -= 1;
    }
    if (t < 1.0f / 6) {
        return p + (q - p) * 6 * t;
    }
    if (t < 0.5f) {
        return q;
    }
    if (t < 2.0f / 3) {
        return p + (q - p) * (2.0f / 3 - t) * 6;
    }
    return p;
}

}  // namespace

bool parseColor(std::string_view in, float rgba[4]) {
    const std::string s = trimStr(in);
    if (s.empty() || s == "none") {
        return false;
    }
    if (s[0] == '#') {
        int d[6] = {0, 0, 0, 0, 0, 0};
        int n = 0;
        for (size_t i = 1; i < s.size() && n < 6; ++i) {
            const int h = hexDig(s[i]);
            if (h < 0) {
                break;
            }
            d[n++] = h;
        }
        if (n == 3) {
            rgba[0] = toLin((float)(d[0] * 17) / 255.0f);
            rgba[1] = toLin((float)(d[1] * 17) / 255.0f);
            rgba[2] = toLin((float)(d[2] * 17) / 255.0f);
            rgba[3] = 1;
            return true;
        }
        if (n == 6) {
            rgba[0] = toLin((float)(d[0] * 16 + d[1]) / 255.0f);
            rgba[1] = toLin((float)(d[2] * 16 + d[3]) / 255.0f);
            rgba[2] = toLin((float)(d[4] * 16 + d[5]) / 255.0f);
            rgba[3] = 1;
            return true;
        }
        return false;
    }
    if (s.compare(0, 4, "rgb(") == 0 || s.compare(0, 5, "rgba(") == 0) {
        const size_t o = s.find('(');
        const size_t c = s.rfind(')');
        if (o == std::string::npos || c == std::string::npos || c <= o) {
            return false;
        }
        double v[4] = {0, 0, 0, 1};
        const int n = splitNums(std::string_view(s).substr(o + 1, c - o - 1), v, 4);
        if (n < 3) {
            return false;
        }
        // Unitless 0..255 maps to 0..1 here.
        for (int k = 0; k < 3; ++k) {
            if (v[k] > 1) {
                v[k] /= 255.0;
            }
        }
        rgba[0] = toLin((float)v[0]);
        rgba[1] = toLin((float)v[1]);
        rgba[2] = toLin((float)v[2]);
        rgba[3] = (float)v[3];
        return true;
    }
    if (s.compare(0, 4, "hsl(") == 0 || s.compare(0, 5, "hsla(") == 0) {
        const size_t o = s.find('(');
        const size_t c = s.rfind(')');
        if (o == std::string::npos || c == std::string::npos || c <= o) {
            return false;
        }
        // Hue may carry deg.
        std::string body(s.substr(o + 1, c - o - 1));
        for (size_t k = 0; (k = body.find("deg", k)) != std::string::npos;) {
            body.erase(k, 3);
        }
        double v[4] = {0, 0, 0, 1};
        const int n = splitNums(body, v, 4);
        if (n < 3) {
            return false;
        }
        float h = (float)(v[0] / 360.0 - std::floor(v[0] / 360.0));
        const float sa = (float)v[1];
        const float li = (float)v[2];
        float r = li, g = li, b = li;
        if (sa != 0) {
            const float q = li < 0.5f ? li * (1 + sa) : li + sa - li * sa;
            const float p = 2 * li - q;
            r = hueChan(p, q, h + 1.0f / 3);
            g = hueChan(p, q, h);
            b = hueChan(p, q, h - 1.0f / 3);
        }
        rgba[0] = toLin(r);
        rgba[1] = toLin(g);
        rgba[2] = toLin(b);
        rgba[3] = (float)v[3];
        return true;
    }
    float lit[4] = {0, 0, 0, 1};
    if (litColor(s, lit)) {
        rgba[0] = lit[0];
        rgba[1] = lit[1];
        rgba[2] = lit[2];
        rgba[3] = lit[3];
        return true;
    }
    return false;
}

std::string paintRef(std::string_view in) {
    const std::string s = trimStr(in);
    if (s.compare(0, 4, "url(") != 0) {
        return "";
    }
    const size_t h = s.find('#');
    if (h == std::string::npos) {
        return "";
    }
    size_t a = h + 1;
    size_t b = s.size();
    while (b > a && (std::isspace((unsigned char)s[b - 1]) || s[b - 1] == '"' ||
                     s[b - 1] == '\'' || s[b - 1] == ')')) {
        --b;
    }
    while (a < b && (s[a] == '"' || s[a] == '\'')) {
        ++a;
    }
    return s.substr(a, b > a ? b - a : 0);
}

Paint parsePaint(std::string_view in) {
    Paint p;
    const std::string s = trimStr(in);
    if (s.empty() || s == "none") {
        p.none = true;
        return p;
    }
    const std::string ref = paintRef(s);
    if (!ref.empty()) {
        p.none = false;
        p.hasRef = true;
        p.ref = ref;
        return p;
    }
    float c[4] = {0, 0, 0, 1};
    if (parseColor(s, c)) {
        p.none = false;
        p.r = c[0];
        p.g = c[1];
        p.b = c[2];
        p.a = c[3];
        return p;
    }
    p.none = true;
    return p;
}

}  // namespace pittore::svg
