// Path data scan over a view. Single pass into PathSeg structs.
#include "engine/vector/svg/path_scan.h"

#include "engine/vector/svg/fast_num.h"

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
            return SegType::Line;
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
        case 'T':
        case 't':
            return SegType::SmoothQuad;
        case 'A':
        case 'a':
            return SegType::Arc;
        default:
            return SegType::Close;
    }
}

bool isBlank(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',';
}

}  // namespace

void scanPathData(std::string_view in, std::vector<PathSeg>& out) {
    const char* p = in.data();
    const char* end = p + in.size();
    auto skip = [&] {
        while (p < end && isBlank(*p)) {
            ++p;
        }
    };
    char cur = 0;
    bool first = true;
    skip();
    while (p < end) {
        if (isCmd(*p)) {
            cur = *p++;
            first = true;
            if (cur == 'Z' || cur == 'z') {
                PathSeg g;
                g.type = SegType::Close;
                g.rel = false;
                g.n = 0;
                out.push_back(g);
                cur = 0;
            }
            skip();
            continue;
        }
        if (cur == 0) {
            ++p;
            skip();
            continue;
        }
        const int want = needCount(cur);
        if (want == 0) {
            cur = 0;
            continue;
        }
        const char* save = p;
        double vv[7] = {0, 0, 0, 0, 0, 0, 0};
        bool good = true;
        for (int k = 0; k < want; ++k) {
            skip();
            if (!scanNumber(p, end, vv[k])) {
                good = false;
                break;
            }
        }
        if (!good) {
            // Resync without losing progress: a command letter takes over,
            // otherwise one char is consumed so bad input cannot hang.
            p = save;
            skip();
            if (p < end && isCmd(*p)) {
                continue;
            }
            if (p < end) {
                ++p;
            }
            skip();
            continue;
        }
        char use = cur;
        if ((cur == 'M' || cur == 'm') && !first) {
            use = (cur == 'M') ? 'L' : 'l';
        }
        PathSeg g;
        g.type = toType(use);
        g.rel = use >= 'a' && use <= 'z';
        for (int k = 0; k < want; ++k) {
            g.v[k] = vv[k];
        }
        g.n = want;
        out.push_back(g);
        first = false;
        skip();
    }
}

}  // namespace pittore::svg
