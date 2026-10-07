#pragma once
// Displacement-mesh warp — the engine behind Liquify. R48: the CPU solves the
// deformed UV map (a coarse grid of "where this result pixel fetches its colour
// from"), a sampler walks the layer through it. The grid lives at 4 px so a
// usable brush covers several cells but a 12 MP layer holds ~750 k offsets
// instead of 12 M; displacement is bilinearly interpolated at sample time.
//
// The mesh and warp kernels implement the Liquify brush set: Forward
// Warp, Reconstruct, Twirl CW/CCW, Pucker, Bloat and Push Left. The brushes
// mutate the mesh; the warp kernel re-samples a frozen snapshot through it.
//
// Two levels of mesh live here. `WarpMesh` covers the whole layer and is what a
// stroke mutates. `WarpSubgrid` is the vertex slice a single region render
// needs, so the GPU path uploads a few kilobytes per dab instead of the whole
// grid; every backend samples the same subgrid, which is what keeps the device
// kernels bit-identical to the CPU reference.

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/pixel.h"

namespace pittore::compute {

// Grid spacing of a displacement mesh, in layer pixels. Small enough that a
// usable brush covers several cells, large enough to keep the mesh cheap.
constexpr float kWarpCell = 4.0f;

// Per-vertex displacement over a rectangle of a layer's NATIVE pixels:
// (dx, dy) = where this point's colour is fetched FROM, relative to itself,
// in layer pixels. Zero everywhere is the identity. `left/top/right/bottom`
// are the mesh's layer-pixel bounds (right/bottom exclusive); the region a dab
// may write is clamped to them so a mesh whose last vertex sits past the layer
// edge never writes out of bounds.
struct WarpMesh {
    int left = 0, top = 0;       // mesh origin, layer pixel coords
    int right = 0, bottom = 0;   // mesh bounds, exclusive (normally w, h)
    std::uint32_t cols = 0;
    std::uint32_t rows = 0;
    std::vector<float> offsets;  // interleaved (dx, dy) per vertex, row-major

    bool identity() const;
};

// The vertex slice a region render needs: the offsets of every mesh vertex
// whose bilinear cell touches [x0,x1) × [y0,y1), with its own origin/counts.
// Sampling it is bit-identical to sampling the full mesh, because the slice
// origin differs from the mesh origin by an exact multiple of the cell size.
struct WarpSubgrid {
    int left = 0, top = 0;       // layer pixel coords of slice vertex (0,0)
    std::uint32_t cols = 0;
    std::uint32_t rows = 0;
    std::vector<float> offsets;  // interleaved (dx, dy), slice-local, row-major

    bool empty() const { return cols < 2 || rows < 2; }
};

// A mesh over the full [0, w) × [0, h) layer, initially the identity.
WarpMesh make_warp_mesh(std::uint32_t w, std::uint32_t h);

// Bilinear displacement at a position in layer pixel coords. Clamps at the
// mesh edge rather than reading out of bounds; positions far outside return
// (0, 0).
void warp_mesh_sample(const WarpMesh& m, float x, float y, float& dx, float& dy);

// Extract the vertex slice needed to render [x0,x1) × [y0,y1) through `m`.
// The rect is clamped to the mesh bounds first; an empty subgrid comes back
// when the rect misses the mesh.
WarpSubgrid warp_subgrid(const WarpMesh& m, int x0, int y0, int x1, int y1);

// Bilinear displacement from a subgrid, layer pixel coords. Same values as
// warp_mesh_sample over the same region; clamps at the slice edge.
void warp_subgrid_sample(const WarpSubgrid& g, float x, float y, float& dx,
                         float& dy);

// The destination pixels a dab at (cx, cy), radius `radius`, can change, as
// [x0,x1) × [y0,y1) in layer pixels. A dab only moves vertices inside its
// radius, and a result pixel interpolates the four vertices around it, so
// influence stops one cell past the brush. x0 >= x1 (or y0 >= y1) when the
// brush misses the mesh.
void warp_dab_rect(const WarpMesh& m, float cx, float cy, float radius, int& x0,
                   int& y0, int& x1, int& y1);

// Visit every mesh vertex within `radius` of (cx, cy), with a smoothstep
// falloff weight w: 1 at the centre, 0 at the rim (a stroke has no hard rim).
// rx/ry = vertex − centre. The callback mutates the vertex's (dx, dy) in
// place. Header-inline so callers can pass a closure cheaply.
template <typename Fn>
void warp_mesh_for_each_near_shaped(WarpMesh& m, float cx, float cy,
                                    float radius, float exp, Fn&& f) {
    if (radius <= 0.0f || m.cols == 0 || m.rows == 0) return;
    const int loC = std::max(0, static_cast<int>(std::floor(
                                    (cx - radius - m.left) / kWarpCell)));
    const int loR = std::max(0, static_cast<int>(std::floor(
                                    (cy - radius - m.top) / kWarpCell)));
    const int hiC = std::min(static_cast<int>(m.cols) - 1,
                             static_cast<int>(std::ceil((cx + radius - m.left) /
                                                        kWarpCell)));
    const int hiR = std::min(static_cast<int>(m.rows) - 1,
                             static_cast<int>(std::ceil((cy + radius - m.top) /
                                                        kWarpCell)));
    for (int r = loR; r <= hiR; ++r) {
        for (int c = loC; c <= hiC; ++c) {
            const float vx = m.left + c * kWarpCell;
            const float vy = m.top + r * kWarpCell;
            const float d = std::hypot(vx - cx, vy - cy);
            if (d >= radius) continue;
            const float t = 1.0f - d / radius;
            const float base = t * t * (3.0f - 2.0f * t);
            const float w = std::pow(std::max(base, 0.0f), exp);
            float* o = &m.offsets[static_cast<std::size_t>(r) * m.cols * 2 +
                                  static_cast<std::size_t>(c) * 2];
            f(o[0], o[1], w, vx - cx, vy - cy);
        }
    }
}

template <typename Fn>
void warp_mesh_for_each_near(WarpMesh& m, float cx, float cy, float radius,
                             Fn&& f) {
    warp_mesh_for_each_near_shaped(m, cx, cy, radius, 1.0f,
                                   static_cast<Fn&&>(f));
}

void warp_mesh_smooth(WarpMesh& m, float cx, float cy, float radius,
                      float amount);

void warp_mesh_clone(WarpMesh& m, float sx, float sy, float dx, float dy,
                     float radius, float strength);

bool warp_mesh_write(const std::string& path, const WarpMesh& m);
bool warp_mesh_read(const std::string& path, WarpMesh& m);

// Reconstruct brush: pull every offset under the brush back towards zero,
// strongest at the centre (`amount` ∈ [0,1] scaled by the same falloff).
void warp_mesh_relax(WarpMesh& m, float cx, float cy, float radius,
                     float amount);

// Resample the region [x0,x1) × [y0,y1) of `src` (a w*h RGBAf layer, straight
// alpha) through the mesh/subgrid into `dst` (same w*h layout, normally the
// live layer). Bilinear on premultiplied alpha so a soft edge does not fringe;
// reads outside the layer are transparent. `dst` must not alias `src` —
// callers warp from a frozen snapshot into the live layer, re-doing only the
// dab's footprint, so a stroke never compounds on itself.
void warp_into_host(RGBAf* dst, const RGBAf* src, std::uint32_t w,
                    std::uint32_t h, const WarpMesh& mesh, int x0, int y0,
                    int x1, int y1);
void warp_into_host(RGBAf* dst, const RGBAf* src, std::uint32_t w,
                    std::uint32_t h, const WarpSubgrid& grid, int x0, int y0,
                    int x1, int y1);

}  // namespace pittore::compute
