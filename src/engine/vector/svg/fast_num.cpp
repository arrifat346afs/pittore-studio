// View-based double scan per SVG number grammar.
#include "engine/vector/svg/fast_num.h"

namespace pittore::svg {

bool scanNumber(const char*& p, const char* end, double& out) {
    const char* s = p;
    bool neg = false;
    if (s < end && (*s == '+' || *s == '-')) {
        neg = *s == '-';
        ++s;
    }
    bool any = false;
    double ip = 0;
    while (s < end && *s >= '0' && *s <= '9') {
        ip = ip * 10 + (*s - '0');
        ++s;
        any = true;
    }
    double fr = 0, sc = 1;
    if (s < end && *s == '.') {
        ++s;
        while (s < end && *s >= '0' && *s <= '9') {
            fr = fr * 10 + (*s - '0');
            sc *= 10;
            ++s;
            any = true;
        }
    }
    if (!any) {
        return false;
    }
    double v = ip + fr / sc;
    if (s < end && (*s == 'e' || *s == 'E')) {
        const char* e0 = s++;
        bool eneg = false;
        if (s < end && (*s == '+' || *s == '-')) {
            eneg = *s == '-';
            ++s;
        }
        int ex = 0;
        bool ed = false;
        while (s < end && *s >= '0' && *s <= '9') {
            ex = ex * 10 + (*s - '0');
            ++s;
            ed = true;
        }
        if (ed) {
            double pw = 1;
            for (int i = 0; i < ex; ++i) {
                pw *= 10;
            }
            v = eneg ? v / pw : v * pw;
        } else {
            s = e0;
        }
    }
    out = neg ? -v : v;
    p = s;
    return true;
}

void scanDoubles(std::string_view in, std::vector<double>& out) {
    const char* p = in.data();
    const char* end = p + in.size();
    for (;;) {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' ||
                           *p == '\r' || *p == ',')) {
            ++p;
        }
        if (p >= end) {
            return;
        }
        double v = 0;
        if (!scanNumber(p, end, v)) {
            return;
        }
        out.push_back(v);
    }
}

}  // namespace pittore::svg
