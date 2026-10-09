// Transform parse. Small scanner, fixed op set.
#include "engine/vector/svg/transform.h"

#include <cctype>
#include <cmath>
#include <cstdlib>

namespace pittore::svg {
namespace {

// Read doubles inside parens.
int readNums(const std::string& s, size_t& i, double* out, int max) {
    int n = 0;
    while (i < s.size() && n < max) {
        while (i < s.size() &&
               (std::isspace((unsigned char)s[i]) || s[i] == ',')) {
            ++i;
        }
        if (i >= s.size() || s[i] == ')') {
            break;
        }
        char* end = nullptr;
        const double v = std::strtod(s.c_str() + i, &end);
        if (end == s.c_str() + i) {
            break;
        }
        out[n++] = v;
        i = (size_t)(end - s.c_str());
    }
    return n;
}

void skipToClose(const std::string& s, size_t& i) {
    const size_t p = s.find(')', i);
    i = p == std::string::npos ? s.size() : p + 1;
}

}  // namespace

Affine identity() {
    return Affine{};
}

Affine multiply(const Affine& x, const Affine& y) {
    Affine o;
    o.a = x.a * y.a + x.c * y.b;
    o.b = x.b * y.a + x.d * y.b;
    o.c = x.a * y.c + x.c * y.d;
    o.d = x.b * y.c + x.d * y.d;
    o.e = x.a * y.e + x.c * y.f + x.e;
    o.f = x.b * y.e + x.d * y.f + x.f;
    return o;
}

bool isIdentity(const Affine& m) {
    return m.a == 1 && m.b == 0 && m.c == 0 && m.d == 1 && m.e == 0 &&
           m.f == 0;
}

Affine parseTransform(std::string_view in) {
    const std::string s(in);
    Affine out = identity();
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() &&
               (std::isspace((unsigned char)s[i]) || s[i] == ',')) {
            ++i;
        }
        size_t k0 = i;
        while (i < s.size() && std::isalpha((unsigned char)s[i])) {
            ++i;
        }
        const std::string op = s.substr(k0, i - k0);
        while (i < s.size() && std::isspace((unsigned char)s[i])) {
            ++i;
        }
        if (i >= s.size() || s[i] != '(') {
            if (!op.empty()) {
                // Skip bad op name.
                continue;
            }
            ++i;
            continue;
        }
        ++i;
        double v[6] = {0, 0, 0, 0, 0, 0};
        const int n = readNums(s, i, v, 6);
        // Consume close.
        while (i < s.size() && s[i] != ')') {
            ++i;
        }
        if (i < s.size()) {
            ++i;
        }
        Affine m = identity();
        bool ok = true;
        if (op == "translate" && (n == 1 || n == 2)) {
            m.e = v[0];
            m.f = n > 1 ? v[1] : 0;
        } else if (op == "scale" && (n == 1 || n == 2)) {
            m.a = v[0];
            m.d = n > 1 ? v[1] : v[0];
        } else if (op == "rotate" && (n == 1 || n == 3)) {
            const double r = v[0] * 3.141592653589793 / 180.0;
            const double co = std::cos(r);
            const double si = std::sin(r);
            Affine t = identity();
            t.a = co;
            t.b = si;
            t.c = -si;
            t.d = co;
            if (n == 3) {
                Affine p1 = identity();
                Affine p2 = identity();
                p1.e = v[1];
                p1.f = v[2];
                p2.e = -v[1];
                p2.f = -v[2];
                m = multiply(multiply(p1, t), p2);
            } else {
                m = t;
            }
        } else if (op == "skewX" && n == 1) {
            m.c = std::tan(v[0] * 3.141592653589793 / 180.0);
        } else if (op == "skewY" && n == 1) {
            m.b = std::tan(v[0] * 3.141592653589793 / 180.0);
        } else if (op == "matrix" && n == 6) {
            m.a = v[0];
            m.b = v[1];
            m.c = v[2];
            m.d = v[3];
            m.e = v[4];
            m.f = v[5];
        } else {
            ok = false;
        }
        if (ok) {
            out = multiply(out, m);
        }
        (void)skipToClose;
    }
    return out;
}

std::string writeTransform(const Affine& m) {
    if (isIdentity(m)) {
        return "";
    }
    char buf[160];
    std::snprintf(buf, sizeof buf, "matrix(%g %g %g %g %g %g)", m.a, m.b,
                  m.c, m.d, m.e, m.f);
    return buf;
}

}  // namespace pittore::svg
