// Palette parsers (GPL text + ASE/ACB binary subsets).
#include "engine/vector/palette.h"

#include <cmath>
#include <sstream>

namespace pittore::vector {
namespace {
std::uint16_t be16(const std::vector<std::uint8_t>& b, size_t o) {
    return (std::uint16_t)((o + 1 < b.size() ? (b[o] << 8) | b[o + 1] : 0));
}
std::uint32_t be32(const std::vector<std::uint8_t>& b, size_t o) {
    std::uint32_t v = 0;
    for (int k = 0; k < 4 && o + k < b.size(); k++) v = (v << 8) | b[o + k];
    return v;
}
// D50 CIELAB → sRGB (Bradford-adapted D65, gamma-encoded). Full-gamut, no CMS.
std::array<std::uint8_t, 4> labToRgb(double L, double a, double b) {
    double fy = (L + 16) / 116.0, fx = fy + a / 500.0, fz = fy - b / 200.0;
    auto f = [](double t) {
        double d = 6.0 / 29.0;
        return t > d ? t * t * t : 3 * d * d * (t - 4.0 / 29.0);
    };
    double X = 0.9642 * f(fx), Y = 1.0 * f(fy), Z = 0.8251 * f(fz);
    // Bradford D50→D65.
    double x = X * 1.0478 + Y * 0.0229 + Z * -0.0501;
    double y = X * 0.0295 + Y * 0.9905 + Z * -0.0201;
    double z = X * -0.0092 + Y * 0.0150 + Z * 0.7521;
    // Linear sRGB.
    double r = 3.2406 * x - 1.5372 * y - 0.4986 * z;
    double g = -0.9689 * x + 1.8758 * y + 0.0415 * z;
    double bl = 0.0557 * x - 0.2040 * y + 1.0570 * z;
    auto gamma = [](double v) {
        v = std::min(1.0, std::max(0.0, v));
        return v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1 / 2.4) - 0.055;
    };
    return {(std::uint8_t)(gamma(r) * 255), (std::uint8_t)(gamma(g) * 255),
            (std::uint8_t)(gamma(bl) * 255), 255};
}
std::array<std::uint8_t, 4> cmykToRgb(double c, double m, double y, double k) {
    auto ch = [](double v) {
        return (std::uint8_t)(std::min(1.0, std::max(0.0, 1 - v)) * 255);
    };
    return {ch(std::min(1.0, c * (1 - k) + k)), ch(std::min(1.0, m * (1 - k) + k)),
            ch(std::min(1.0, y * (1 - k) + k)), 255};
}
}  // namespace

Palette parseGpl(const std::string& text) {
    Palette p;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("Name:", 0) == 0) {
            p.name = line.substr(5);
            while (!p.name.empty() && (p.name.front() == ' ')) p.name.erase(p.name.begin());
        } else if (!line.empty() && std::isdigit((unsigned char)line[0])) {
            std::istringstream ls(line);
            int r, g, b;
            ls >> r >> g >> b;
            std::string name;
            std::getline(ls, name);
            while (!name.empty() && (name.front() == ' ' || name.front() == '\t'))
                name.erase(name.begin());
            PaletteColor c;
            c.name = name.empty() ? "Untitled" : name;
            c.rgba = {(std::uint8_t)r, (std::uint8_t)g, (std::uint8_t)b, 255};
            p.colors.push_back(c);
        }
    }
    return p;
}

Palette parseAse(const std::vector<std::uint8_t>& bytes) {
    Palette p;
    p.name = "ASE";
    if (bytes.size() < 12) return p;
    // "ASEF" + version + block count.
    size_t o = 12;
    std::uint32_t n = be32(bytes, 8);
    for (std::uint32_t i = 0; i < n && o + 6 < bytes.size(); i++) {
        std::uint16_t type = be16(bytes, o);
        std::uint32_t len = be32(bytes, o + 2);
        size_t e = o + 6 + len;
        if (type == 0x0001 && o + 8 < bytes.size()) {  // color entry
            std::uint16_t nlen = be16(bytes, o + 6);
            std::string name;
            for (int k = 0; k < nlen - 1 && o + 8 + k * 2 + 1 < bytes.size(); k++)
                name += (char)bytes[o + 8 + k * 2 + 1];
            size_t co = o + 8 + (size_t)nlen * 2;
            std::string mode;
            for (int k = 0; k < 4 && co + k < bytes.size(); k++) mode += (char)bytes[co + k];
            PaletteColor c;
            c.name = name.empty() ? "ASE" : name;
            auto f32 = [&](size_t oo) -> float {
                // be32 assembles the big-endian bits into a host value, so a
                // plain copy yields the float (no byte swap needed).
                std::uint32_t v = be32(bytes, oo);
                float f;
                __builtin_memcpy(&f, &v, 4);
                return f;
            };
            if (mode == "RGB " && co + 18 <= bytes.size()) {
                c.rgba = {(std::uint8_t)(f32(co + 4) * 255), (std::uint8_t)(f32(co + 8) * 255),
                          (std::uint8_t)(f32(co + 12) * 255), 255};
                p.colors.push_back(c);
            } else if (mode == "Gray" && co + 10 <= bytes.size()) {
                std::uint8_t g = (std::uint8_t)(f32(co + 4) * 255);
                c.rgba = {g, g, g, 255};
                p.colors.push_back(c);
            } else if (mode == "CMYK" && co + 22 <= bytes.size()) {
                c.rgba = cmykToRgb(f32(co + 4), f32(co + 8), f32(co + 12), f32(co + 16));
                c.name += " (CMYK→sRGB)";
                p.colors.push_back(c);
            } else if (mode == "LAB " && co + 18 <= bytes.size()) {
                c.rgba = labToRgb(f32(co + 4) * 100, f32(co + 8) * 128,
                                  f32(co + 12) * 128);
                p.colors.push_back(c);
            }
        }
        o = e;
    }
    return p;
}

Palette parseAcb(const std::vector<std::uint8_t>& bytes) {
    Palette p;
    p.name = "ACB";
    // Structured pass: book title, then per-color name/code/LAB triplets.
    // Falls back to the name+triplet scan when the header disagrees, so
    // third-party books still load instead of failing.
    size_t o = 0;
    auto pascal = [&](std::string& out) -> bool {
        if (o >= bytes.size()) return false;
        std::uint8_t nl = bytes[o++];
        if (o + nl > bytes.size()) return false;
        out.assign((const char*)&bytes[o], nl);
        o += nl;
        return true;
    };
    std::string title;
    size_t mark = o;
    bool structured = pascal(title);
    if (structured) {
        p.name = title.empty() ? "ACB" : title;
        // Color count follows the title block in most books (uint16 BE).
        std::uint16_t count = 0;
        if (o + 2 <= bytes.size()) {
            count = be16(bytes, o);
        }
        if (count > 0 && count < 2048) {
            o += 2;
            for (int i = 0; i < count && o < bytes.size(); i++) {
                std::string name;
                if (!pascal(name)) break;
                // 6-byte vendor code + LAB triplet (int16 BE each, L 0..10000).
                if (o + 12 > bytes.size()) break;
                o += 6;
                std::int16_t L = (std::int16_t)be16(bytes, o);
                std::int16_t A = (std::int16_t)be16(bytes, o + 2);
                std::int16_t B = (std::int16_t)be16(bytes, o + 4);
                o += 6;
                if (L < 0 || L > 10000) break;
                PaletteColor c;
                c.name = name.empty() ? "ACB" : name;
                c.rgba = labToRgb(L / 100.0, A / 100.0, B / 100.0);
                p.colors.push_back(c);
            }
            if (!p.colors.empty()) return p;
        }
        o = mark;  // header disagreed — fall through to the scan
        p.colors.clear();
    }
    while (o + 8 < bytes.size() && p.colors.size() < 512) {
        std::uint8_t nl = bytes[o];
        if (nl > 0 && nl < 64 && o + 1 + nl + 8 < bytes.size()) {
            std::string name((char*)&bytes[o + 1], nl);
            size_t co = o + 1 + nl;
            // Heuristic: three int16 LAB after a 6-byte code.
            std::int16_t L = (std::int16_t)be16(bytes, co + 6);
            if (L >= 0 && L <= 10000) {
                std::int16_t A = (std::int16_t)be16(bytes, co + 8);
                std::int16_t B = (std::int16_t)be16(bytes, co + 10);
                PaletteColor c;
                c.name = name;
                c.rgba = labToRgb(L / 100.0, A / 100.0, B / 100.0);
                p.colors.push_back(c);
                o = co + 12;
                continue;
            }
        }
        o++;
    }
    return p;
}

std::string writeGpl(const Palette& palette) {
    std::string s = "GIMP Palette\nName: " + palette.name + "\nColumns: 8\n#\n";
    for (auto& c : palette.colors) {
        s += std::to_string(c.rgba[0]) + " " + std::to_string(c.rgba[1]) + " " +
             std::to_string(c.rgba[2]) + "\t" + c.name + "\n";
    }
    return s;
}

}  // namespace pittore::vector
