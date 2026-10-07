// CPU application of SVG filter primitives.
#include "engine/vector/filter_fe.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace pittore::vector {
namespace {

float attrF(const FePrimitive& p, const char* k, float dflt) {
    auto it = p.attrs.find(k);
    if (it == p.attrs.end()) return dflt;
    try {
        return std::stof(it->second);
    } catch (...) {
        return dflt;
    }
}

std::string attrS(const FePrimitive& p, const char* k, const std::string& dflt) {
    auto it = p.attrs.find(k);
    return it == p.attrs.end() ? dflt : it->second;
}

std::vector<float> parseFloats(const std::string& s, size_t want) {
    std::vector<float> out;
    size_t i = 0;
    while (i < s.size() && out.size() < (want ? want : 256)) {
        while (i < s.size() &&
               (s[i] == ' ' || s[i] == ',' || s[i] == '\t' || s[i] == '\n'))
            i++;
        if (i >= s.size()) break;
        size_t j = i;
        while (j < s.size() && s[j] != ' ' && s[j] != ',' && s[j] != '\t' && s[j] != '\n')
            j++;
        try {
            out.push_back(std::stof(s.substr(i, j - i)));
        } catch (...) {
            break;
        }
        i = j;
    }
    return out;
}

RgbaImage boxBlur(const RgbaImage& src, float radius) {
    if (radius <= 0 || src.w <= 0) return src;
    int r = (int)std::ceil(radius);
    RgbaImage tmp = src, out = src;
    for (int y = 0; y < src.h; y++)
        for (int x = 0; x < src.w; x++)
            for (int c = 0; c < 4; c++) {
                int acc = 0, n = 0;
                for (int k = -r; k <= r; k++) {
                    int xx = std::min(src.w - 1, std::max(0, x + k));
                    acc += src.px[(size_t)((y * src.w + xx) * 4 + c)];
                    n++;
                }
                tmp.px[(size_t)((y * src.w + x) * 4 + c)] = (std::uint8_t)(acc / n);
            }
    for (int y = 0; y < src.h; y++)
        for (int x = 0; x < src.w; x++)
            for (int c = 0; c < 4; c++) {
                int acc = 0, n = 0;
                for (int k = -r; k <= r; k++) {
                    int yy = std::min(src.h - 1, std::max(0, y + k));
                    acc += tmp.px[(size_t)((yy * src.w + x) * 4 + c)];
                    n++;
                }
                out.px[(size_t)((y * src.w + x) * 4 + c)] = (std::uint8_t)(acc / n);
            }
    return out;
}

// Value-noise turbulence (tileable, deterministic): enough for gallery
// previews and displacement-map sources without a full Perlin port.
RgbaImage turbulence(int w, int h, float baseFreq, int octaves, int seed) {    RgbaImage out;
    out.w = w;
    out.h = h;
    out.px.resize((size_t)(w * h * 4));
    auto hash = [&](int x, int y, int s) -> float {
        unsigned v = (unsigned)(x * 374761393 + y * 668265263 + s * 1442695041);
        v = (v ^ (v >> 13)) * 1274126177;
        return ((v ^ (v >> 16)) & 0xFFFF) / 65535.0f;
    };
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float acc = 0, amp = 1, tot = 0;
            float fx = x * baseFreq, fy = y * baseFreq;
            for (int o = 0; o < octaves; o++) {
                int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
                float tx = fx - x0, ty = fy - y0;
                float a = hash(x0, y0, seed + o), b = hash(x0 + 1, y0, seed + o);
                float c = hash(x0, y0 + 1, seed + o), d = hash(x0 + 1, y0 + 1, seed + o);
                float v = a * (1 - tx) * (1 - ty) + b * tx * (1 - ty) +
                          c * (1 - tx) * ty + d * tx * ty;
                acc += v * amp;
                tot += amp;
                amp *= 0.5f;
                fx *= 2;
                fy *= 2;
            }
            std::uint8_t g = (std::uint8_t)(acc / (tot > 0 ? tot : 1) * 255);
            size_t o = (size_t)((y * w + x) * 4);
            out.px[o] = out.px[o + 1] = out.px[o + 2] = g;
            out.px[o + 3] = 255;
        }
    return out;
}

}  // namespace

RgbaImage applyFilterGraph(const FilterGraph& graph, const RgbaImage& src) {
    if (graph.prims.empty() || src.w <= 0) return src;
    std::map<std::string, RgbaImage> slots;
    slots["SourceGraphic"] = src;
    RgbaImage cur = src;
    auto input = [&](const FePrimitive& p) -> RgbaImage {
        if (p.in.empty() || p.in == "SourceGraphic") return cur;
        auto it = slots.find(p.in);
        return it == slots.end() ? cur : it->second;
    };
    for (const auto& p : graph.prims) {
        RgbaImage in = input(p);
        RgbaImage out = in;
        if (p.type == "feGaussianBlur") {
            out = boxBlur(in, attrF(p, "stdDeviation", 1.0f));
            // Two passes approximate Gaussian.
            out = boxBlur(out, attrF(p, "stdDeviation", 1.0f) * 0.5f);
        } else if (p.type == "feOffset") {
            float dx = attrF(p, "dx", 0), dy = attrF(p, "dy", 0);
            out.px.assign((size_t)(in.w * in.h * 4), 0);
            for (int y = 0; y < in.h; y++)
                for (int x = 0; x < in.w; x++) {
                    int sx = x - (int)dx, sy = y - (int)dy;
                    if (sx < 0 || sy < 0 || sx >= in.w || sy >= in.h) continue;
                    for (int c = 0; c < 4; c++)
                        out.px[(size_t)((y * in.w + x) * 4 + c)] =
                            in.px[(size_t)((sy * in.w + sx) * 4 + c)];
                }
        } else if (p.type == "feFlood") {
            out.px.assign((size_t)(in.w * in.h * 4), 0);
            // flood-color kept simple: parse #rrggbb if present.
            std::uint8_t r = 0, g = 0, b = 0;
            auto it = p.attrs.find("flood-color");
            if (it != p.attrs.end() && it->second.size() >= 7 && it->second[0] == '#') {
                try {
                    r = (std::uint8_t)std::stoul(it->second.substr(1, 2), nullptr, 16);
                    g = (std::uint8_t)std::stoul(it->second.substr(3, 2), nullptr, 16);
                    b = (std::uint8_t)std::stoul(it->second.substr(5, 2), nullptr, 16);
                } catch (...) {
                }
            }
            float op = attrF(p, "flood-opacity", 1.0f);
            for (size_t i = 0; i < (size_t)in.w * in.h; i++) {
                out.px[i * 4] = r;
                out.px[i * 4 + 1] = g;
                out.px[i * 4 + 2] = b;
                out.px[i * 4 + 3] = (std::uint8_t)(op * 255);
            }
        } else if (p.type == "feComposite") {
            auto it2 = slots.find(p.in2);
            RgbaImage b = it2 == slots.end() ? src : it2->second;
            std::string op = "over";
            auto oi = p.attrs.find("operator");
            if (oi != p.attrs.end()) op = oi->second;
            for (size_t i = 0; i < (size_t)in.w * in.h * 4; i++) {
                if (op == "in")
                    out.px[i] = (std::uint8_t)(in.px[i] * b.px[i] / 255);
                else if (op == "out")
                    out.px[i] = (std::uint8_t)(in.px[i] * (255 - b.px[i]) / 255);
                else if (op == "xor")
                    out.px[i] = (std::uint8_t)(std::abs((int)in.px[i] - (int)b.px[i]));
                else if (op == "arithmetic") {
                    // Per-pixel k1*i1*i2 + k2*i1 + k3*i2 + k4 (normalized).
                    float k1 = attrF(p, "k1", 0), k2 = attrF(p, "k2", 0),
                          k3 = attrF(p, "k3", 0), k4 = attrF(p, "k4", 0);
                    float i1 = in.px[i] / 255.0f, i2 = b.px[i] / 255.0f;
                    out.px[i] =
                        (std::uint8_t)(std::clamp(k1 * i1 * i2 + k2 * i1 + k3 * i2 + k4,
                                                  0.0f, 1.0f) *
                                       255);
                } else  // over
                    out.px[i] = in.px[i];
            }
        } else if (p.type == "feColorMatrix") {
            std::string t = "matrix";
            auto ti = p.attrs.find("type");
            if (ti != p.attrs.end()) t = ti->second;
            if (t == "saturate") {
                float s = attrF(p, "values", 1.0f);
                for (int i = 0; i < in.w * in.h; i++) {
                    float r = in.px[(size_t)(i * 4)], g = in.px[(size_t)(i * 4 + 1)],
                          b = in.px[(size_t)(i * 4 + 2)];
                    float l = 0.3f * r + 0.6f * g + 0.1f * b;
                    out.px[(size_t)(i * 4)] = (std::uint8_t)(l + (r - l) * s);
                    out.px[(size_t)(i * 4 + 1)] = (std::uint8_t)(l + (g - l) * s);
                    out.px[(size_t)(i * 4 + 2)] = (std::uint8_t)(l + (b - l) * s);
                }
            } else if (t == "hueRotate") {
                float deg = attrF(p, "values", 0.0f);
                float c = std::cos(deg * 3.14159265f / 180.0f);
                float s = std::sin(deg * 3.14159265f / 180.0f);
                // Standard hue-rotate matrix (luma axis).
                float m[3][3] = {
                    {0.213f + c * 0.787f - s * 0.213f, 0.715f - c * 0.715f - s * 0.715f,
                     0.072f - c * 0.072f + s * 0.928f},
                    {0.213f - c * 0.213f + s * 0.143f, 0.715f + c * 0.285f + s * 0.140f,
                     0.072f - c * 0.072f - s * 0.283f},
                    {0.213f - c * 0.213f - s * 0.787f, 0.715f - c * 0.715f + s * 0.715f,
                     0.072f + c * 0.928f + s * 0.072f},
                };
                for (int i = 0; i < in.w * in.h; i++) {
                    float r = in.px[(size_t)(i * 4)] / 255.0f;
                    float g = in.px[(size_t)(i * 4 + 1)] / 255.0f;
                    float b = in.px[(size_t)(i * 4 + 2)] / 255.0f;
                    out.px[(size_t)(i * 4)] = (std::uint8_t)(std::clamp(
                        m[0][0] * r + m[0][1] * g + m[0][2] * b, 0.0f, 1.0f) * 255);
                    out.px[(size_t)(i * 4 + 1)] = (std::uint8_t)(std::clamp(
                        m[1][0] * r + m[1][1] * g + m[1][2] * b, 0.0f, 1.0f) * 255);
                    out.px[(size_t)(i * 4 + 2)] = (std::uint8_t)(std::clamp(
                        m[2][0] * r + m[2][1] * g + m[2][2] * b, 0.0f, 1.0f) * 255);
                }
            } else if (t == "luminanceToAlpha") {
                for (int i = 0; i < in.w * in.h; i++) {
                    float r = in.px[(size_t)(i * 4)], g = in.px[(size_t)(i * 4 + 1)],
                          b = in.px[(size_t)(i * 4 + 2)];
                    std::uint8_t l = (std::uint8_t)(0.3f * r + 0.6f * g + 0.1f * b);
                    out.px[(size_t)(i * 4)] = out.px[(size_t)(i * 4 + 1)] =
                        out.px[(size_t)(i * 4 + 2)] = 0;
                    out.px[(size_t)(i * 4 + 3)] = l;
                }
            } else {
                // Generic 4x5 matrix (20 values, row-major, +bias column).
                auto vals = parseFloats(attrS(p, "values", ""), 20);
                if (vals.size() == 20) {
                    for (int i = 0; i < in.w * in.h; i++) {
                        float v[4] = {in.px[(size_t)(i * 4)] / 255.0f,
                                      in.px[(size_t)(i * 4 + 1)] / 255.0f,
                                      in.px[(size_t)(i * 4 + 2)] / 255.0f,
                                      in.px[(size_t)(i * 4 + 3)] / 255.0f};
                        for (int r = 0; r < 4; r++) {
                            float o = vals[(size_t)(r * 5 + 4)];
                            for (int c = 0; c < 4; c++) o += vals[(size_t)(r * 5 + c)] * v[c];
                            out.px[(size_t)(i * 4 + r)] =
                                (std::uint8_t)(std::clamp(o, 0.0f, 1.0f) * 255);
                        }
                    }
                }
            }
        } else if (p.type == "feComponentTransfer") {
            auto func = [&](const std::string& ch, float x) {
                std::string pre = ch + ".";
                auto get = [&](const std::string& k) -> std::string {
                    auto it = p.attrs.find(pre + k);
                    return it == p.attrs.end() ? "" : it->second;
                };
                std::string t = get("type");
                if (t.empty() || t == "identity") return x;
                auto vals = parseFloats(get("tableValues"), 64);
                if (t == "table" && vals.size() >= 2) {
                    float f = std::clamp(x, 0.0f, 1.0f) * (vals.size() - 1);
                    size_t k = std::min(vals.size() - 2, (size_t)f);
                    return vals[k] + (vals[k + 1] - vals[k]) * (f - k);
                }
                if (t == "discrete" && !vals.empty()) {
                    size_t k = std::min(vals.size() - 1,
                                        (size_t)(std::clamp(x, 0.0f, 1.0f) * vals.size()));
                    return vals[k];
                }
                if (t == "linear" && vals.size() >= 2)
                    return vals[0] * x + vals[1];
                if (t == "gamma" && vals.size() >= 3)
                    return vals[0] * std::pow(std::max(x, 0.0f), vals[1]) + vals[2];
                return x;
            };
            const char* chs[4] = {"feFuncR", "feFuncG", "feFuncB", "feFuncA"};
            for (int i = 0; i < in.w * in.h; i++)
                for (int c = 0; c < 4; c++)
                    out.px[(size_t)(i * 4 + c)] = (std::uint8_t)(
                        std::clamp(func(chs[c], in.px[(size_t)(i * 4 + c)] / 255.0f), 0.0f,
                                   1.0f) *
                        255);
        } else if (p.type == "feMorphology") {
            int r = (int)std::ceil(attrF(p, "radius", 1.0f));
            std::string op = attrS(p, "operator", "erode");
            for (int y = 0; y < in.h; y++)
                for (int x = 0; x < in.w; x++)
                    for (int c = 0; c < 4; c++) {
                        int v = (op == "dilate") ? 0 : 255;
                        for (int ky = -r; ky <= r; ky++)
                            for (int kx = -r; kx <= r; kx++) {
                                int xx = std::min(in.w - 1, std::max(0, x + kx));
                                int yy = std::min(in.h - 1, std::max(0, y + ky));
                                int s = in.px[(size_t)((yy * in.w + xx) * 4 + c)];
                                v = (op == "dilate") ? std::max(v, s) : std::min(v, s);
                            }
                        out.px[(size_t)((y * in.w + x) * 4 + c)] = (std::uint8_t)v;
                    }
        } else if (p.type == "feConvolveMatrix") {
            int order = (int)attrF(p, "order", 3);
            auto kvals = parseFloats(attrS(p, "kernelMatrix", ""), (size_t)order * order);
            float divisor = attrF(p, "divisor", 1.0f);
            if (divisor == 0) divisor = 1;
            float bias = attrF(p, "bias", 0.0f);
            int half = order / 2;
            for (int y = 0; y < in.h; y++)
                for (int x = 0; x < in.w; x++)
                    for (int c = 0; c < 4; c++) {
                        float acc = 0;
                        for (int ky = 0; ky < order; ky++)
                            for (int kx = 0; kx < order; kx++) {
                                int xx = std::min(in.w - 1, std::max(0, x + kx - half));
                                int yy = std::min(in.h - 1, std::max(0, y + ky - half));
                                float k = kvals.size() == (size_t)order * order
                                              ? kvals[(size_t)(ky * order + kx)]
                                              : (kx == half && ky == half ? 1 : 0);
                                acc += k * in.px[(size_t)((yy * in.w + xx) * 4 + c)];
                            }
                        out.px[(size_t)((y * in.w + x) * 4 + c)] =
                            (std::uint8_t)std::clamp(acc / divisor + bias * 255, 0.0f,
                                                     255.0f);
                    }
        } else if (p.type == "feDisplacementMap") {
            auto it2 = slots.find(p.in2);
            RgbaImage disp = it2 == slots.end() ? src : it2->second;
            float scale = attrF(p, "scale", 0.0f);
            std::string xc = attrS(p, "xChannelSelector", "A");
            std::string yc = attrS(p, "yChannelSelector", "A");
            auto chan = [&](const RgbaImage& im, int x, int y, const std::string& s) {
                x = std::min(im.w - 1, std::max(0, x));
                y = std::min(im.h - 1, std::max(0, y));
                size_t o = (size_t)(y * im.w + x) * 4;
                int c = (s == "R") ? 0 : (s == "G") ? 1 : (s == "B") ? 2 : 3;
                return im.px[o + c] / 255.0f;
            };
            for (int y = 0; y < in.h; y++)
                for (int x = 0; x < in.w; x++) {
                    int sx = (int)(x + scale * (chan(disp, x, y, xc) - 0.5));
                    int sy = (int)(y + scale * (chan(disp, x, y, yc) - 0.5));
                    sx = std::min(in.w - 1, std::max(0, sx));
                    sy = std::min(in.h - 1, std::max(0, sy));
                    for (int c = 0; c < 4; c++)
                        out.px[(size_t)((y * in.w + x) * 4 + c)] =
                            in.px[(size_t)((sy * in.w + sx) * 4 + c)];
                }
        } else if (p.type == "feDiffuseLighting" || p.type == "feSpecularLighting") {
            // Height-from-luminance Lambert/Blinn shading with a distant light
            // (folded "light.*" attrs from feDistantLight).
            float surf = attrF(p, "surfaceScale", 1.0f);
            float az = attrF(p, "light.azimuth", 235.0f) * 3.14159265f / 180.0f;
            float el = attrF(p, "light.elevation", 55.0f) * 3.14159265f / 180.0f;
            float lx = std::cos(el) * std::cos(az), ly = std::cos(el) * std::sin(az),
                  lz = std::sin(el);
            std::uint8_t lr = 255, lg = 255, lb = 255;
            auto li = p.attrs.find("lighting-color");
            if (li != p.attrs.end() && li->second.size() >= 7 && li->second[0] == '#') {
                try {
                    lr = (std::uint8_t)std::stoul(li->second.substr(1, 2), nullptr, 16);
                    lg = (std::uint8_t)std::stoul(li->second.substr(3, 2), nullptr, 16);
                    lb = (std::uint8_t)std::stoul(li->second.substr(5, 2), nullptr, 16);
                } catch (...) {
                }
            }
            bool spec = p.type == "feSpecularLighting";
            float exp = attrF(p, "specularExponent", 8.0f);
            auto height = [&](int x, int y) {
                x = std::min(in.w - 1, std::max(0, x));
                y = std::min(in.h - 1, std::max(0, y));
                size_t o = (size_t)(y * in.w + x) * 4;
                return (0.3f * in.px[o] + 0.6f * in.px[o + 1] + 0.1f * in.px[o + 2]) /
                       255.0f * surf;
            };
            for (int y = 0; y < in.h; y++)
                for (int x = 0; x < in.w; x++) {
                    float nx = (height(x - 1, y) - height(x + 1, y)) / 2;
                    float ny = (height(x, y - 1) - height(x, y + 1)) / 2;
                    float nz = 1.0f;
                    float il = std::sqrt(nx * nx + ny * ny + nz * nz);
                    float d = std::max(0.0f, (nx * lx + ny * ly + nz * lz) / il);
                    float v = spec ? std::pow(d, exp) : d;
                    size_t o = (size_t)(y * in.w + x) * 4;
                    out.px[o] = (std::uint8_t)(lr * v);
                    out.px[o + 1] = (std::uint8_t)(lg * v);
                    out.px[o + 2] = (std::uint8_t)(lb * v);
                    out.px[o + 3] = in.px[o + 3];
                }
        } else if (p.type == "feTile") {
            for (int y = 0; y < in.h; y++)
                for (int x = 0; x < in.w; x++)
                    for (int c = 0; c < 4; c++)
                        out.px[(size_t)((y * in.w + x) * 4 + c)] =
                            in.px[(size_t)((y * in.w + x) * 4 + c)];
        } else if (p.type == "feMerge") {
            out.px.assign((size_t)(in.w * in.h * 4), 0);
            auto over = [&](const RgbaImage& fg) {
                for (int i = 0; i < in.w * in.h; i++) {
                    int sa = fg.px[(size_t)(i * 4 + 3)], da = out.px[(size_t)(i * 4 + 3)];
                    int oa = sa + da * (255 - sa) / 255;
                    for (int c = 0; c < 3; c++)
                        out.px[(size_t)(i * 4 + c)] = oa
                            ? (std::uint8_t)((fg.px[(size_t)(i * 4 + c)] * sa +
                                              out.px[(size_t)(i * 4 + c)] * da *
                                                  (255 - sa) / 255) /
                                             oa)
                            : 0;
                    out.px[(size_t)(i * 4 + 3)] = (std::uint8_t)oa;
                }
            };
            std::string nodes = attrS(p, "nodes", "");
            if (nodes.empty()) {
                over(in);
            } else {
                size_t s = 0;
                while (s <= nodes.size()) {
                    size_t c = nodes.find(',', s);
                    std::string nm = nodes.substr(
                        s, c == std::string::npos ? std::string::npos : c - s);
                    auto it = slots.find(nm);
                    over(it == slots.end() ? in : it->second);
                    if (c == std::string::npos) break;
                    s = c + 1;
                }
            }
        } else if (p.type == "feDropShadow") {
            float dx = attrF(p, "dx", 2.0f), dy = attrF(p, "dy", 2.0f);
            float sd = attrF(p, "stdDeviation", 2.0f);
            std::uint8_t fr = 0, fg = 0, fb = 0;
            auto fi = p.attrs.find("flood-color");
            if (fi != p.attrs.end() && fi->second.size() >= 7 && fi->second[0] == '#') {
                try {
                    fr = (std::uint8_t)std::stoul(fi->second.substr(1, 2), nullptr, 16);
                    fg = (std::uint8_t)std::stoul(fi->second.substr(3, 2), nullptr, 16);
                    fb = (std::uint8_t)std::stoul(fi->second.substr(5, 2), nullptr, 16);
                } catch (...) {
                }
            }
            float fo = attrF(p, "flood-opacity", 1.0f);
            RgbaImage sh = in;
            for (int i = 0; i < in.w * in.h; i++) {
                sh.px[(size_t)(i * 4)] = fr;
                sh.px[(size_t)(i * 4 + 1)] = fg;
                sh.px[(size_t)(i * 4 + 2)] = fb;
                sh.px[(size_t)(i * 4 + 3)] =
                    (std::uint8_t)(in.px[(size_t)(i * 4 + 3)] * fo);
            }
            sh = boxBlur(sh, sd);
            sh = boxBlur(sh, sd * 0.5f);
            out.px.assign((size_t)(in.w * in.h * 4), 0);
            for (int y = 0; y < in.h; y++)
                for (int x = 0; x < in.w; x++) {
                    int sx = x - (int)dx, sy = y - (int)dy;
                    RgbaImage over = in;
                    if (sx >= 0 && sy >= 0 && sx < in.w && sy < in.h) {
                        int sa = sh.px[(size_t)((sy * in.w + sx) * 4 + 3)];
                        int da = in.px[(size_t)((y * in.w + x) * 4 + 3)];
                        int oa = sa + da * (255 - sa) / 255;
                        for (int c = 0; c < 3; c++)
                            out.px[(size_t)((y * in.w + x) * 4 + c)] = oa
                                ? (std::uint8_t)(
                                      (sh.px[(size_t)((sy * in.w + sx) * 4 + c)] * sa +
                                       in.px[(size_t)((y * in.w + x) * 4 + c)] * da *
                                           (255 - sa) / 255) /
                                      oa)
                                : 0;
                        out.px[(size_t)((y * in.w + x) * 4 + 3)] = (std::uint8_t)oa;
                    } else {
                        for (int c = 0; c < 4; c++)
                            out.px[(size_t)((y * in.w + x) * 4 + c)] =
                                in.px[(size_t)((y * in.w + x) * 4 + c)];
                    }
                    (void)over;
                }
        } else if (p.type == "feTurbulence") {
            float f = attrF(p, "baseFrequency", 0.05f);
            int oct = (int)attrF(p, "numOctaves", 2);
            out = turbulence(in.w, in.h, f, oct, 7);
        } else if (p.type == "feBlend") {
            auto it2 = slots.find(p.in2);
            RgbaImage b = it2 == slots.end() ? src : it2->second;
            std::string mode = "normal";
            auto mi = p.attrs.find("mode");
            if (mi != p.attrs.end()) mode = mi->second;
            for (int i = 0; i < in.w * in.h; i++) {
                for (int c = 0; c < 3; c++) {
                    int a = in.px[(size_t)(i * 4 + c)], bb = b.px[(size_t)(i * 4 + c)];
                    int v = a;
                    if (mode == "multiply")
                        v = a * bb / 255;
                    else if (mode == "screen")
                        v = 255 - (255 - a) * (255 - bb) / 255;
                    else if (mode == "darken")
                        v = std::min(a, bb);
                    else if (mode == "lighten")
                        v = std::max(a, bb);
                    out.px[(size_t)(i * 4 + c)] = (std::uint8_t)v;
                }
            }
        }
        // Unknown primitives pass through but stay addressable via result=.
        if (!p.result.empty()) slots[p.result] = out;
        cur = out;
    }
    return cur;
}

FilterGraph filterGraphFromSvg(const std::string& id,
                               const std::vector<FePrimitive>& prims) {
    FilterGraph g;
    g.id = id;
    g.prims = prims;
    return g;
}

std::string filterGraphToSvg(const FilterGraph& graph) {
    std::string s = "<filter id=\"" + graph.id + "\">";
    for (const auto& p : graph.prims) {
        s += "<" + p.type;
        for (const auto& [k, v] : p.attrs) {
            // Folded children serialize back as elements, not attributes.
            if (k.rfind("light.", 0) == 0 || k.rfind("feFunc", 0) == 0 ||
                k == "nodes")
                continue;
            s += " " + k + "=\"" + v + "\"";
        }
        if (!p.result.empty()) s += " result=\"" + p.result + "\"";
        // Re-emit folded children.
        std::string kids;
        for (const auto& [k, v] : p.attrs) {
            if (k.rfind("light.", 0) == 0)
                kids += "";
            else if (k == "nodes") {
                size_t st = 0;
                while (st <= v.size()) {
                    size_t c = v.find(',', st);
                    kids += "<feMergeNode in=\"" +
                            v.substr(st, c == std::string::npos ? std::string::npos
                                                               : c - st) +
                            "\"/>";
                    if (c == std::string::npos) break;
                    st = c + 1;
                }
            }
        }
        // Light element + func elements (reconstruct tag from prefix).
        std::map<std::string, std::map<std::string, std::string>> groups;
        for (const auto& [k, v] : p.attrs) {
            size_t dot = k.find('.');
            if (dot == std::string::npos) continue;
            std::string tag = k.substr(0, dot), at = k.substr(dot + 1);
            if (tag.rfind("feFunc", 0) == 0 || tag == "light") groups[tag][at] = v;
        }
        for (const auto& [tag, attrs] : groups) {
            std::string ltag = tag;
            std::map<std::string, std::string> clean = attrs;
            if (tag == "light") {
                auto ti = clean.find("tag");
                ltag = ti == clean.end() ? "feDistantLight" : ti->second;
                clean.erase("tag");
            }
            kids += "<" + ltag;
            for (const auto& [ak, av] : clean) kids += " " + ak + "=\"" + av + "\"";
            kids += "/>";
        }
        if (kids.empty()) {
            s += "/>";
        } else {
            s += ">" + kids + "</" + p.type + ">";
        }
    }
    s += "</filter>";
    return s;
}

const std::vector<FilterCatalogEntry>& svgFilterCatalog() {
    static const std::vector<FilterCatalogEntry> k = {
        {"blur-subtle", "Subtle blur", "Blur"}, {"blur-strong", "Heavy blur", "Blur"},
        {"shadow-drop", "Drop shadow", "Shadows"}, {"shadow-inner", "Inner shadow", "Shadows"},
        {"glow-neon", "Neon glow", "Light"}, {"light-emboss", "Emboss", "Light"},
        {"grain-film", "Film grain", "Noise"}, {"grain-turbulence", "Turbulence", "Noise"},
        {"paint-oil", "Oil paint", "Artistic"}, {"paint-watercolor", "Watercolor", "Artistic"},
        {"edge-detect", "Edge detect", "Edges"}, {"edge-chisel", "Chisel", "Edges"},
        {"morph-erode", "Erode", "Morph"}, {"morph-dilate", "Dilate", "Morph"},
        {"displace-wave", "Wave displace", "Distort"}, {"displace-ripple", "Ripple", "Distort"},
        {"color-sepia", "Sepia", "Color"}, {"color-duotone", "Duotone", "Color"},
        {"tile-mosaic", "Mosaic tile", "Tiles"}, {"tile-kaleido", "Kaleidoscope", "Tiles"},
    };
    return k;
}

}  // namespace pittore::vector
