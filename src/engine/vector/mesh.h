#pragma once
// Mesh gradients: Coons-patch colour interpolation.
//
// A mesh is a grid of tensor-product patches (mesh/patch/row); each corner
// carries a colour (+ optional side control points). Evaluation
// is bilinear on the corner colours with bilinear patch geometry -- exact for
// axis-aligned quads, a close visual match for warped patches, and
// toolkit-free. Conical (sweep) gradients ride the same header as a
// single-ring special case used by the editor UI.
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace pittore::vector {

// One tensor patch: 4 corners with colours, 4x4 lattice collapsed to the
// bilinear corners + edge midpoints the editor exposes (16 points total to
// stay compatible with SVG mesh proposals: corners + 2 controls per edge).
struct MeshPatch {
    // Geometry: p[16] in row-major 4x4 lattice order.
    std::array<std::array<double, 2>, 16> p{};
    // Corner colours: c[4] = TL, TR, BR, BL (premultiplied? no -- straight).
    std::array<std::array<std::uint8_t, 4>, 4> c{};
};

// A rows x cols lattice of patches sharing edges.
struct MeshGradient {
    int rows = 1, cols = 1;
    std::vector<MeshPatch> patches;  // rows*cols, row-major
    bool isConical = false;          // sweep rendering (angle around centre)
    double conicalCx = 0.5, conicalCy = 0.5;
};

// Sample the mesh at normalized (u,v) in [0,1]^2 across the whole lattice.
// Returns straight RGBA8. Out-of-range clamps to the edge.
std::array<std::uint8_t, 4> meshSample(const MeshGradient& mesh, double u, double v);

// Rasterize into an RGBA8 buffer (row-major, w*h*4). Used by the shape
// rasterizer and the SVG exporter fallback.
std::vector<std::uint8_t> rasterizeMesh(const MeshGradient& mesh, int w, int h);

// Minimal SVG serialization (<meshgradient> proposal + fallback comment so
// strict viewers still see a flat fill). `fallbackRgba` paints viewers that
// do not understand meshes.
std::string meshToSvg(const MeshGradient& mesh, const std::string& id,
                      const std::array<std::uint8_t, 4>& fallbackRgba);

}  // namespace pittore::vector
