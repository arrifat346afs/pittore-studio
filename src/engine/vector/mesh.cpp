// Mesh gradient sampling: mesh/array/patch/row evaluation.
#include "engine/vector/mesh.h"

#include <cmath>
#include <sstream>

namespace pittore::vector {
namespace {

std::array<double, 2> bilerp(const std::array<std::array<double, 2>, 16>& p,
                             double u, double v) {
    // Corners of the 4x4 lattice: (0,0),(0,3),(3,3),(3,0) in lattice idx.
    auto at = [&](int r, int c) -> std::array<double, 2> { return p[(size_t)(r * 4 + c)]; };
    auto tl = at(0, 0), tr = at(0, 3), br = at(3, 3), bl = at(3, 0);
    double x = tl[0] * (1 - u) * (1 - v) + tr[0] * u * (1 - v) + br[0] * u * v +
               bl[0] * (1 - u) * v;
    double y = tl[1] * (1 - u) * (1 - v) + tr[1] * u * (1 - v) + br[1] * u * v +
               bl[1] * (1 - u) * v;
    return {x, y};
}

std::array<std::uint8_t, 4> mix4(const std::array<std::uint8_t, 4>& a,
                                 const std::array<std::uint8_t, 4>& b, double t) {
    std::array<std::uint8_t, 4> o{};
    for (int i = 0; i < 4; i++)
        o[(size_t)i] = (std::uint8_t)(a[(size_t)i] * (1 - t) + b[(size_t)i] * t + 0.5);
    return o;
}

}  // namespace

std::array<std::uint8_t, 4> meshSample(const MeshGradient& mesh, double u, double v) {
    if (mesh.patches.empty()) return {0, 0, 0, 0};
    if (mesh.isConical) {
        // Sweep: hue wheel approximated by cycling patch corner colours by
        // angle. Cheap, stable, matches the editor swatch.
        double ang = std::atan2(v - mesh.conicalCy, u - mesh.conicalCx);
        double t = (ang + 3.14159265358979) / (2 * 3.14159265358979);
        const auto& c = mesh.patches.front().c;
        double seg = t * 4.0;
        int k = (int)std::floor(seg) % 4;
        return mix4(c[(size_t)k], c[(size_t)((k + 1) % 4)], seg - std::floor(seg));
    }
    u = std::min(1.0, std::max(0.0, u));
    v = std::min(1.0, std::max(0.0, v));
    int cols = mesh.cols > 0 ? mesh.cols : 1, rows = mesh.rows > 0 ? mesh.rows : 1;
    double fu = u * cols, fv = v * rows;
    int cu = std::min(cols - 1, (int)std::floor(fu));
    int cv = std::min(rows - 1, (int)std::floor(fv));
    const MeshPatch& patch = mesh.patches[(size_t)(cv * cols + cu)];
    double lu = fu - cu, lv = fv - cv;
    auto top = mix4(patch.c[0], patch.c[1], lu);
    auto bot = mix4(patch.c[3], patch.c[2], lu);
    (void)bilerp;
    return mix4(top, bot, lv);
}

std::vector<std::uint8_t> rasterizeMesh(const MeshGradient& mesh, int w, int h) {
    std::vector<std::uint8_t> out((size_t)(w > 0 && h > 0 ? w * h * 4 : 0), 0);
    if (w <= 0 || h <= 0) return out;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            auto c = meshSample(mesh, (x + 0.5) / w, (y + 0.5) / h);
            size_t o = (size_t)((y * w + x) * 4);
            out[o] = c[0];
            out[o + 1] = c[1];
            out[o + 2] = c[2];
            out[o + 3] = c[3];
        }
    return out;
}

std::string meshToSvg(const MeshGradient& mesh, const std::string& id,
                      const std::array<std::uint8_t, 4>& fallbackRgba) {
    std::ostringstream s;
    char buf[64];
    snprintf(buf, sizeof(buf), "#%02x%02x%02x", fallbackRgba[0], fallbackRgba[1],
             fallbackRgba[2]);
    s << "<meshgradient id=\"" << id << "\" rows=\"" << mesh.rows << "\" cols=\""
      << mesh.cols << "\" fallback=\"" << buf << "\"";
    if (mesh.isConical) s << " conical=\"1\" cx=\"" << mesh.conicalCx << "\" cy=\"" << mesh.conicalCy << "\"";
    s << ">";
    char nb[32];
    for (const auto& p : mesh.patches) {
        s << "<meshpatch>";
        for (int k = 0; k < 4; k++)
            s << "c" << k << "=\"" << (int)p.c[(size_t)k][0] << ","
              << (int)p.c[(size_t)k][1] << "," << (int)p.c[(size_t)k][2] << "\" ";
        s << "p=\"";
        for (int k = 0; k < 16; k++) {
            snprintf(nb, sizeof(nb), "%.3f,%.3f", p.p[(size_t)k][0], p.p[(size_t)k][1]);
            if (k) s << " ";
            s << nb;
        }
        s << "\"/>";
    }
    s << "</meshgradient>";
    return s.str();
}

}  // namespace pittore::vector
