// Text flow layouts.
#include "engine/vector/text_flow.h"

#include <cmath>

namespace pittore::vector {

std::vector<FlowGlyph> textOnPathLayout(const std::string& text,
                                        const std::vector<std::pair<double, double>>& spine,
                                        const std::vector<double>& glyphAdvance,
                                        double letterSpacing, double startOffset) {
    std::vector<FlowGlyph> out;
    if (spine.size() < 2) return out;
    // Arclength table.
    std::vector<double> cum{0};
    for (size_t i = 1; i < spine.size(); i++)
        cum.push_back(cum.back() + std::hypot(spine[i].first - spine[i - 1].first,
                                              spine[i].second - spine[i - 1].second));
    double s = startOffset;
    size_t si = 0;
    // Decode UTF-8 to codepoints (ASCII fast path, BMP fallback).
    std::vector<char32_t> cps;
    for (size_t i = 0; i < text.size();) {
        unsigned char c = (unsigned char)text[i];
        if (c < 0x80) {
            cps.push_back(c);
            i++;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < text.size()) {
            cps.push_back(((c & 0x1F) << 6) | (text[i + 1] & 0x3F));
            i += 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < text.size()) {
            cps.push_back(((c & 0x0F) << 12) | ((text[i + 1] & 0x3F) << 6) |
                          (text[i + 2] & 0x3F));
            i += 3;
        } else {
            cps.push_back(0xFFFD);
            i++;
        }
    }
    for (size_t k = 0; k < cps.size(); k++) {
        double adv = k < glyphAdvance.size() ? glyphAdvance[k] : 10.0;
        double mid = s + adv / 2;
        while (si + 1 < cum.size() && cum[si + 1] < mid) si++;
        FlowGlyph g;
        g.ch = cps[k];
        g.advance = adv;
        if (si + 1 >= spine.size() || mid > cum.back()) {
            g.visible = false;
            out.push_back(g);
            s += adv + letterSpacing;
            continue;
        }
        double segLen = cum[si + 1] - cum[si];
        double t = segLen > 1e-9 ? (mid - cum[si]) / segLen : 0;
        double x0 = spine[si].first, y0 = spine[si].second;
        double x1 = spine[si + 1].first, y1 = spine[si + 1].second;
        g.x = x0 + (x1 - x0) * t;
        g.y = y0 + (y1 - y0) * t;
        g.rotationDeg = std::atan2(y1 - y0, x1 - x0) * 180.0 / 3.14159265358979;
        out.push_back(g);
        s += adv + letterSpacing;
    }
    return out;
}

std::vector<FlowGlyph> textInShapeLayout(const std::string& text,
                                          const std::vector<std::array<double, 4>>& columns,
                                          const std::vector<double>& glyphAdvance,
                                          double lineHeight, bool wrap) {
    std::vector<FlowGlyph> out;
    if (columns.empty()) return out;
    size_t col = 0;
    double cx = columns[0][0], cy = columns[0][1] + lineHeight * 0.8;
    auto colW = [&](size_t c) { return columns[c][2] - columns[c][0]; };
    size_t gi = 0;
    for (size_t i = 0; i < text.size();) {
        if (text[i] == '\n') {
            cx = columns[col][0];
            cy += lineHeight;
            i++;
            continue;
        }
        // Word measure for wrapping.
        size_t wEnd = i;
        while (wEnd < text.size() && text[wEnd] != ' ' && text[wEnd] != '\n') wEnd++;
        double wAdv = 0;
        for (size_t k = i; k < wEnd; k++)
            wAdv += (gi + (k - i)) < glyphAdvance.size() ? glyphAdvance[gi + (k - i)] : 8.0;
        if (wrap && cx + wAdv > columns[col][2] && cx > columns[col][0]) {
            cx = columns[col][0];
            cy += lineHeight;
        }
        if (cy > columns[col][3]) {
            if (col + 1 >= columns.size()) break;
            col++;
            cx = columns[col][0];
            cy = columns[col][1] + lineHeight * 0.8;
            continue;
        }
        char32_t cp = (unsigned char)text[i];
        double adv = gi < glyphAdvance.size() ? glyphAdvance[gi] : 8.0;
        FlowGlyph g;
        g.ch = cp;
        g.x = cx;
        g.y = cy;
        g.advance = adv;
        out.push_back(g);
        cx += adv;
        if (text[i] == ' ') {
            // Collapse handled by wrap measure; keep the space advance.
        }
        gi++;
        i++;
        (void)colW;
    }
    return out;
}

}  // namespace pittore::vector
