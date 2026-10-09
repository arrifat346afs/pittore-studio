// Path data scanner. Numbers follow SVG grammar.
#include "engine/vector/svg/path_data.h"

#include <cctype>
#include <cstdlib>

namespace pittore::svg {
namespace {

bool isCmd(char c) {
    return c == 'M' || c == 'm' || c == 'L' || c == 'l' || c == 'H' ||
           c == 'h' || c == 'V' || c == 'v' || c == 'C' || c == 'c' ||
           c == 'S' || c == 's' || c == 'Q' || c == 'q' || c == 'T' ||
           c == 't' || c == 'A' || c == 'a' || c == 'Z' || c == 'z';
}

int needCount(char c) {
    switch (c) {
        case 'M':
        case 'm':
        case 'L':
        case 'l':
        case 'T':
        case 't':
            return 2;
        case 'H':
        case 'h':
        case 'V':
        case 'v':
            return 1;
        case 'C':
        case 'c':
            return 6;
        case 'S':
        case 's':
        case 'Q':
        case 'q':
            return 4;
        case 'A':
        case 'a':
            return 7;
        default:
            return 0;
    }
}

SegType toType(char c) {
    switch (c) {
        case 'M':
        case 'm':
            return SegType::Move;
        case 'L':
        case 'l':
        case 'T':
        case 't':
            // T is kept as line here; smooth pass runs later.
            return (c == 'T' || c == 't') ? SegType::SmoothQuad : SegType::Line;
        case 'H':
        case 'h':
            return SegType::H;
        case 'V':
        case 'v':
            return SegType::V;
        case 'C':
        case 'c':
            return SegType::Cubic;
        case 'S':
        case 's':
            return SegType::SmoothCubic;
        case 'Q':
        case 'q':
            return SegType::Quad;
        case 'A':
        case 'a':
            return SegType::Arc;
        default:
            return SegType::Close;
    }
}

// Try one number at i. Sets ok=false when absent.
double readNum(const std::string& s, size_t& i, bool& ok) {
    while (i < s.size() &&
           (std::isspace((unsigned char)s[i]) || s[i] == ',')) {
        ++i;
    }
    if (i >= s.size()) {
        ok = false;
        return 0;
    }
    if (isCmd(s[i])) {
        ok = false;
        return 0;
    }
    char* end = nullptr;
    const double v = std::strtod(s.c_str() + i, &end);
    if (end == s.c_str() + i) {
        ok = false;
        return 0;
    }
    i = (size_t)(end - s.c_str());
    ok = true;
    return v;
}

}  // namespace

std::vector<PathSeg> parsePathData(std::string_view in) {
    const std::string s(in);
    std::vector<PathSeg> out;
    size_t i = 0;
    char cmd = 0;
    while (i < s.size()) {
        while (i < s.size() &&
               (std::isspace((unsigned char)s[i]) || s[i] == ',')) {
            ++i;
        }
        if (i >= s.size()) {
            break;
        }
        if (isCmd(s[i])) {
            cmd = s[i++];
            if (cmd == 'Z' || cmd == 'z') {
                PathSeg g;
                g.type = SegType::Close;
                out.push_back(g);
                cmd = 0;
                continue;
            }
        }
        if (cmd == 0) {
            // Stray number with no command.
            break;
        }
        const int want = needCount(cmd);
        if (want == 0) {
            break;
        }
        // First set uses cmd as-is; extra sets repeat.
        bool first = true;
        while (true) {
            size_t save = i;
            double vv[7] = {0, 0, 0, 0, 0, 0, 0};
            bool good = true;
            for (int k = 0; k < want; ++k) {
                bool ok = false;
                vv[k] = readNum(s, i, ok);
                if (!ok) {
                    good = false;
                    break;
                }
            }
            if (!good) {
                i = save;
                // Resync with guaranteed progress: a following command
                // takes over, otherwise one char is consumed so bad input
                // cannot spin here.
                size_t peek = i;
                while (peek < s.size() &&
                       (std::isspace((unsigned char)s[peek]) ||
                        s[peek] == ',')) {
                    ++peek;
                }
                if (peek < s.size() && isCmd(s[peek])) {
                    i = peek;
                } else if (i < s.size()) {
                    ++i;
                }
                break;
            }
            PathSeg g;
            char use = cmd;
            // Extra Move pairs become Line pairs.
            if ((cmd == 'M' || cmd == 'm') && !first) {
                use = (cmd == 'M') ? 'L' : 'l';
            }
            g.type = toType(use);
            g.rel = use >= 'a' && use <= 'z';
            if (use == 'T' || use == 't') {
                g.rel = use == 't';
            }
            for (int k = 0; k < want; ++k) {
                g.v[k] = vv[k];
            }
            g.n = want;
            out.push_back(g);
            first = false;
            size_t peek = i;
            while (peek < s.size() &&
                   (std::isspace((unsigned char)s[peek]) || s[peek] == ',')) {
                ++peek;
            }
            if (peek < s.size() && isCmd(s[peek])) {
                break;
            }
        }
    }
    return out;
}

}  // namespace pittore::svg
