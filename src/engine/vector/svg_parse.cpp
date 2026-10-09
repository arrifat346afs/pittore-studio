#include "engine/vector/svg_parse.h"

#include <cctype>
#include <cstdlib>
#include <cmath>
#include <string_view>
#include <vector>

#include "engine/vector/vector_scene.h"

namespace {

float srgb_to_linear(float c) {
    return c <= 0.04045f ? c / 12.92f
                         : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Hex (#rgb / #rrggbb) or a small named set; converts sRGB->linear.
bool parse_color(std::string_view v, float out[4]) {
    if (v.empty()) return false;
    if (v[0] == '#') {
        int vals[6];
        int n = 0;
        for (std::size_t i = 1; i < v.size() && n < 6; ++i) {
            const int d = hex_digit(v[i]);
            if (d < 0) break;
            vals[n++] = d;
        }
        if (n == 3) {
            out[0] = srgb_to_linear(float(vals[0] * 17) / 255.0f);
            out[1] = srgb_to_linear(float(vals[1] * 17) / 255.0f);
            out[2] = srgb_to_linear(float(vals[2] * 17) / 255.0f);
            out[3] = 1.0f;
            return true;
        }
        if (n == 6) {
            out[0] = srgb_to_linear(float(vals[0] * 16 + vals[1]) / 255.0f);
            out[1] = srgb_to_linear(float(vals[2] * 16 + vals[3]) / 255.0f);
            out[2] = srgb_to_linear(float(vals[4] * 16 + vals[5]) / 255.0f);
            out[3] = 1.0f;
            return true;
        }
        return false;
    }
    if (v == "black") { out[0] = out[1] = out[2] = 0.0f; out[3] = 1.0f; return true; }
    if (v == "white") { out[0] = out[1] = out[2] = 1.0f; out[3] = 1.0f; return true; }
    if (v == "red")   { out[0] = 1.0f; out[1] = out[2] = 0.0f; out[3] = 1.0f; return true; }
    if (v == "green") { out[1] = 1.0f; out[0] = out[2] = 0.0f; out[3] = 1.0f; return true; }
    if (v == "blue")  { out[2] = 1.0f; out[0] = out[1] = 0.0f; out[3] = 1.0f; return true; }
    if (v == "gray" || v == "grey") {
        const float g = srgb_to_linear(0.502f);
        out[0] = out[1] = out[2] = g;
        out[3] = 1.0f;
        return true;
    }
    return false;
}

bool parse_color_alpha(std::string_view v, float* col, float extra_alpha) {
    if (v.empty() || v == "none") return false;
    if (!parse_color(v, col)) return false;
    col[3] = col[3] * extra_alpha;
    return true;
}

float parse_float(std::string_view s, float fallback = 0.0f) {
    if (s.empty()) return fallback;
    const std::string tmp(s);
    return std::strtof(tmp.c_str(), nullptr);
}

bool ends_with(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() &&
           s.substr(s.size() - suffix.size()) == suffix;
}

float parse_length(std::string_view s, float ref) {
    if (s.empty()) return 0.0f;
    if (ends_with(s, "%"))
        return parse_float(s.substr(0, s.size() - 1)) / 100.0f * ref;
    return parse_float(s);
}

bool is_name_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '-' ||
           c == '_' || c == ':';
}

std::string_view read_token(std::string_view s, std::size_t& i) {
    const std::size_t start = i;
    while (i < s.size() && is_name_char(s[i])) ++i;
    return s.substr(start, i - start);
}

struct Attr {
    std::string_view key;
    std::string_view value;
};

bool read_attr(std::string_view s, std::size_t& i, Attr& a) {
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n'))
        ++i;
    if (i >= s.size()) return false;
    const std::size_t kstart = i;
    while (i < s.size() && is_name_char(s[i])) ++i;
    a.key = s.substr(kstart, i - kstart);
    if (a.key.empty()) return false;  // hit tag end; caller breaks
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
        ++i;
    if (i >= s.size() || s[i] != '=') {
        a.value = {};
        return true;
    }
    ++i;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
        ++i;
    if (i >= s.size()) return false;
    const char q = s[i];
    if (q != '"' && q != '\'') return false;
    ++i;
    const std::size_t vstart = i;
    while (i < s.size() && s[i] != q) ++i;
    if (i >= s.size()) return false;
    a.value = s.substr(vstart, i - vstart);
    ++i;
    return true;
}

void parse_stop_style(std::string_view style, float out[4]) {
    out[0] = out[1] = out[2] = 0.0f;
    out[3] = 1.0f;
    std::string_view rest = style;
    while (!rest.empty()) {
        const std::size_t semi = rest.find(';');
        const std::string_view decl = rest.substr(0, semi);
        const std::size_t colon = decl.find(':');
        if (colon != std::string_view::npos) {
            const std::string_view key = decl.substr(0, colon);
            std::string_view value = decl.substr(colon + 1);
            const std::size_t b = value.find_first_not_of(" \t");
            const std::size_t e = value.find_last_not_of(" \t");
            if (b != std::string_view::npos)
                value = value.substr(b, e - b + 1);
            if (key == "stop-color") {
                float col[4];
                if (parse_color(value, col)) {
                    out[0] = col[0];
                    out[1] = col[1];
                    out[2] = col[2];
                }
            } else if (key == "stop-opacity") {
                out[3] = parse_float(value, 1.0f);
            }
        }
        if (semi == std::string_view::npos) break;
        rest.remove_prefix(semi + 1);
    }
}

}  // namespace

namespace pittore::vector {

namespace {

struct GradDef {
    std::string id;
    int kind = 0;  // 0 linear, 1 radial
    float x1 = 0.0f, y1 = 0.0f;
    float x2 = 1.0f, y2 = 0.0f;
    float cx = 0.5f, cy = 0.5f, cr = 0.5f;
    int stop_begin = 0;
    int stop_count = 0;
};

struct Parser {
    VectorScene scene;
    float view_w = 0.0f, view_h = 0.0f;
    std::vector<GradDef> grads;

    // url(#id) fills can reference gradients defined later in the file, so
    // prims are held with grad=-2 and resolved after the linear scan.
    std::vector<std::size_t> pending;
    std::vector<std::string> pending_ids;

    void emit(const VecPrim& p) {
        ++scene.raw_prim_count;
        if (!scene.prims.empty()) {
            const VecPrim& last = scene.prims.back();
            // Consecutive identical fully-opaque flat prims merge: source-over
            // is idempotent for them and 100mb.svg stacks 1.8M identical
            // rects, which would otherwise dominate the scene store.
            if (last.opacity == 1.0f && p.opacity == 1.0f &&
                last.paint == PaintKind::Flat && p.paint == PaintKind::Flat &&
                last.kind == p.kind && last.x == p.x && last.y == p.y &&
                last.w == p.w && last.h == p.h && last.r == p.r &&
                last.sw == p.sw && last.cr == p.cr && last.cg == p.cg &&
                last.cb == p.cb) {
                return;
            }
        }
        scene.prims.push_back(p);
        if (p.grad == -2) {
            pending.push_back(scene.prims.size() - 1);
            pending_ids.push_back(last_url);
        }
    }
    std::string last_url;
};

}  // namespace

VectorScene parse_svg(const std::string& xml, const std::string& name) {
    Parser P;
    P.scene.source_name = name;

    std::string_view s = xml;
    std::size_t i = 0;
    int cur_grad = -1;

    while (i < s.size()) {
        if (s[i] != '<') { ++i; continue; }
        ++i;
        if (i >= s.size()) break;

        if (s[i] == '!' || s[i] == '?') {  // <?xml ?>, <!-- -->, <!DOCTYPE>
            const std::size_t close = s.find('>', i);
            i = close == std::string_view::npos ? s.size() : close + 1;
            continue;
        }

        const bool closing = s[i] == '/';
        if (closing) ++i;
        const std::string_view tag = read_token(s, i);

        // Closing tags carry no attrs; never run open-tag logic for them
        // (</svg> must not reset the viewport, </circle> must not emit).
        if (closing) {
            if (tag == "linearGradient" || tag == "radialGradient") {
                cur_grad = -1;
            }
            while (i < s.size() && s[i] != '>') ++i;
            if (i < s.size()) ++i;
            continue;
        }

        std::string_view x, y, w, h, r, cx, cy;
        std::string_view x1, y1, x2, y2;
        std::string_view fill, stroke, sw, id, style, offset;
        std::string_view stop_color, stop_opacity;
        std::string_view anchor, baseline, dy, font_size, vb, opacity;
        bool have_fill = false;

        Attr a;
        while (read_attr(s, i, a)) {
            if (a.value.empty()) continue;
            if (a.key == "x") x = a.value;
            else if (a.key == "y") y = a.value;
            else if (a.key == "width") w = a.value;
            else if (a.key == "height") h = a.value;
            else if (a.key == "r") r = a.value;
            else if (a.key == "cx") cx = a.value;
            else if (a.key == "cy") cy = a.value;
            else if (a.key == "x1") x1 = a.value;
            else if (a.key == "y1") y1 = a.value;
            else if (a.key == "x2") x2 = a.value;
            else if (a.key == "y2") y2 = a.value;
            else if (a.key == "fill") { fill = a.value; have_fill = true; }
            else if (a.key == "stroke") stroke = a.value;
            else if (a.key == "stroke-width") sw = a.value;
            else if (a.key == "opacity") opacity = a.value;
            else if (a.key == "id") id = a.value;
            else if (a.key == "style") style = a.value;
            else if (a.key == "offset") offset = a.value;
            else if (a.key == "stop-color") stop_color = a.value;
            else if (a.key == "stop-opacity") stop_opacity = a.value;
            else if (a.key == "text-anchor") anchor = a.value;
            else if (a.key == "dominant-baseline") baseline = a.value;
            else if (a.key == "dy") dy = a.value;
            else if (a.key == "font-size") font_size = a.value;
            else if (a.key == "viewBox") vb = a.value;
        }
        // absorb up to '>' (handles both "..." and "... />")
        while (i < s.size() && s[i] != '>') ++i;
        if (i < s.size()) ++i;

        if (tag == "svg") {
            if (!vb.empty()) {
                std::string tmp(vb);
                char* end = nullptr;
                const char* p = tmp.c_str();
                float vals[4] = {0, 0, 0, 0};
                for (int n = 0; n < 4; ++n) {
                    vals[n] = std::strtof(p, &end);
                    if (end == p) break;
                    p = end;
                }
                P.view_w = vals[2];
                P.view_h = vals[3];
            } else {
                P.view_w = parse_length(w, 1000.0f);
                P.view_h = parse_length(h, 1000.0f);
            }
            if (P.view_w <= 0.0f) P.view_w = 1000.0f;
            if (P.view_h <= 0.0f) P.view_h = 1000.0f;
            P.scene.view_w = P.view_w;
            P.scene.view_h = P.view_h;
            continue;
        }

        if (tag == "linearGradient" || tag == "radialGradient") {
            GradDef g;
            g.id = std::string(id);
            g.kind = tag == "linearGradient" ? 0 : 1;
            g.x1 = parse_float(x1) / 100.0f;
            g.y1 = parse_float(y1) / 100.0f;
            g.x2 = parse_float(x2) / 100.0f;
            g.y2 = parse_float(y2) / 100.0f;
            g.cx = parse_float(cx) / 100.0f;
            g.cy = parse_float(cy) / 100.0f;
            g.cr = parse_float(r) / 100.0f;
            g.stop_begin = static_cast<int>(P.scene.stops.size());
            g.stop_count = 0;
            P.grads.push_back(std::move(g));
            cur_grad = static_cast<int>(P.grads.size()) - 1;
            continue;
        }

        if (tag == "stop" && cur_grad >= 0) {
            VecGradStop st;
            st.offset = parse_float(offset) / 100.0f;
            st.r = st.g = st.b = 0.0f;
            st.a = 1.0f;
            float col[4];
            if (!style.empty()) {
                parse_stop_style(style, col);
            } else {
                float ca[4];
                if (parse_color(stop_color, ca)) {
                    col[0] = ca[0]; col[1] = ca[1]; col[2] = ca[2]; col[3] = 1.0f;
                } else {
                    col[0] = col[1] = col[2] = 0.0f; col[3] = 1.0f;
                }
                if (!stop_opacity.empty()) col[3] = parse_float(stop_opacity, 1.0f);
            }
            st.r = col[0]; st.g = col[1]; st.b = col[2]; st.a = col[3];
            P.scene.stops.push_back(st);
            ++P.grads[static_cast<std::size_t>(cur_grad)].stop_count;
            continue;
        }

        if (tag == "rect") {
            VecPrim p;
            p.kind = PrimKind::Rect;
            p.x = parse_length(x, P.view_w);
            p.y = parse_length(y, P.view_h);
            p.w = parse_length(w, P.view_w);
            p.h = parse_length(h, P.view_h);
            p.r = 0.0f;
            p.sw = 0.0f;
            const float alpha = parse_float(opacity, 1.0f);
            if (have_fill && fill.size() > 4 && fill.substr(0, 4) == "url(") {
                std::string_view inner = fill.substr(4);
                if (!inner.empty() && inner.back() == ')')
                    inner = inner.substr(0, inner.size() - 1);
                if (!inner.empty() && inner[0] == '#')
                    inner = inner.substr(1);
                P.last_url = std::string(inner);
                p.paint = PaintKind::LinearGrad;
                p.grad = -2;
                p.opacity = alpha;
                P.emit(p);
                continue;
            }
            float col[4] = {0, 0, 0, 1};
            if (parse_color_alpha(fill, col, alpha)) {
                p.paint = PaintKind::Flat;
                p.grad = -1;
                p.cr = col[0]; p.cg = col[1]; p.cb = col[2];
                p.opacity = col[3];
                P.emit(p);
            }
            continue;
        }

        if (tag == "circle") {
            VecPrim p;
            p.kind = PrimKind::Circle;
            p.x = parse_length(cx, P.view_w);
            p.y = parse_length(cy, P.view_h);
            p.r = parse_length(r, P.view_w < P.view_h ? P.view_w : P.view_h);
            p.w = p.h = 2.0f * p.r;
            p.sw = parse_float(sw, 0.0f);
            const float alpha = parse_float(opacity, 1.0f);
            float col[4] = {0, 0, 0, 1};
            if (parse_color_alpha(fill, col, alpha)) {
                p.paint = PaintKind::Flat;
                p.grad = -1;
                p.cr = col[0]; p.cg = col[1]; p.cb = col[2];
                p.opacity = col[3];
            } else {
                p.opacity = 0.0f;
            }
            if (parse_color(stroke, col)) {
                p.sr = col[0]; p.sg = col[1]; p.sb = col[2];
            } else {
                p.sr = p.sg = p.sb = 0.0f;
            }
            P.emit(p);
            continue;
        }

        if (tag == "text") {
            VecPrim p;
            p.kind = PrimKind::TextBox;
            const float fs = parse_float(font_size, 16.0f);
            const float base_x = parse_length(x, P.view_w);
            float base_y = parse_length(y, P.view_h);
            base_y += parse_float(dy, 0.0f) * fs;

            // Gather glyph count from inline content up to "</text>".
            const std::size_t body_start = i;
            while (i < s.size() && !(s[i] == '<' && i + 6 < s.size() &&
                                     s.substr(i, 7) == "</text>"))
                ++i;
            std::string_view body = s.substr(body_start, i - body_start);
            i = (i < s.size()) ? i + 7 : i;  // consume "</text>"

            int nchars = 0;
            for (char c : body) {
                if (c != ' ' && c != '\t' && c != '\n') ++nchars;
            }
            const float box_w = static_cast<float>(nchars) * 0.62f * fs;
            const float box_h = 1.3f * fs;
            float px, py;
            if (anchor == "middle") {
                px = base_x - box_w / 2.0f;
            } else if (anchor == "end") {
                px = base_x - box_w;
            } else {
                px = base_x;
            }
            if (baseline == "middle") {
                py = base_y - box_h / 2.0f;
            } else if (baseline == "hanging") {
                py = base_y;
            } else {
                py = base_y - box_h;  // alphabetic baseline ~ box bottom
            }
            p.x = px;
            p.y = py;
            p.w = box_w;
            p.h = box_h;
            p.r = 0.0f;
            p.sw = 0.0f;
            const float alpha = parse_float(opacity, 1.0f);
            float col[4] = {0, 0, 0, 1};
            if (have_fill && parse_color_alpha(fill, col, alpha)) {
                p.paint = PaintKind::Flat;
                p.grad = -1;
                p.cr = col[0]; p.cg = col[1]; p.cb = col[2];
                p.opacity = col[3];
            } else {
                p.paint = PaintKind::Flat;
                p.grad = -1;
                p.cr = col[0]; p.cg = col[1]; p.cb = col[2];
                p.opacity = col[3] * alpha;
            }
            P.emit(p);
            continue;
        }
    }

    P.scene.view_w = P.scene.view_w > 0.0f ? P.scene.view_w : 1000.0f;
    P.scene.view_h = P.scene.view_h > 0.0f ? P.scene.view_h : 1000.0f;

    for (std::size_t gi = 0; gi < P.grads.size(); ++gi) {
        const GradDef& g = P.grads[gi];
        VecGrad vg;
        vg.kind = g.kind;
        vg.stop_begin = g.stop_begin;
        vg.stop_count = g.stop_count;
        vg.x1 = g.x1; vg.y1 = g.y1;
        vg.x2 = g.x2; vg.y2 = g.y2;
        vg.cx = g.cx; vg.cy = g.cy;
        vg.r = g.cr;
        P.scene.grads.push_back(vg);
    }
    for (std::size_t k = 0; k < P.pending.size(); ++k) {
        VecPrim& pr = P.scene.prims[P.pending[k]];
        int match = -1;
        for (std::size_t gi = 0; gi < P.grads.size(); ++gi) {
            if (P.grads[gi].id == P.pending_ids[k]) match = static_cast<int>(gi);
        }
        if (match >= 0) {
            pr.grad = match;
            pr.paint = P.grads[static_cast<std::size_t>(match)].kind == 0
                           ? PaintKind::LinearGrad
                           : PaintKind::RadialGrad;
        } else {
            pr.grad = -1;
            pr.paint = PaintKind::Flat;
            pr.opacity = 0.0f;
        }
    }

    return P.scene;
}

}  // namespace pittore::vector