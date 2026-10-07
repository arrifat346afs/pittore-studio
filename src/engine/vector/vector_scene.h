#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pittore::vector {

enum class PrimKind { Rect, Circle, TextBox };
enum class PaintKind { Flat, LinearGrad, RadialGrad };

struct VecGradStop {
    float offset = 0.0f;
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
};

struct VecGrad {
    int kind = 0;              // 0 linear, 1 radial
    int stop_begin = 0;
    int stop_count = 0;
    float x1 = 0.0f, y1 = 0.0f, x2 = 1.0f, y2 = 0.0f;  // linear (normalized)
    float cx = 0.5f, cy = 0.5f, r = 0.5f;               // radial (normalized)
};

struct VecPrim {
    PrimKind kind = PrimKind::Rect;
    PaintKind paint = PaintKind::Flat;
    int grad = -1;              // index into scene.grads, or -1
    float x = 0.0f, y = 0.0f;   // rect: top-left; circle: center
    float w = 0.0f, h = 0.0f;   // rect size; circle: w*h is bbox size
    float r = 0.0f;             // circle radius / text box approx
    float cr = 0.0f, cg = 0.0f, cb = 0.0f;  // flat color
    float opacity = 1.0f;
    float sw = 0.0f;            // stroke width (circle only for now)
    float sr = 0.0f, sg = 0.0f, sb = 0.0f;  // stroke color
};

struct VectorScene {
    float view_w = 0.0f, view_h = 0.0f;
    std::size_t raw_prim_count = 0;  // pre-coalescing count
    std::vector<VecPrim> prims;
    std::vector<VecGrad> grads;
    std::vector<VecGradStop> stops;
    std::string source_name;
};

VectorScene parse_svg(const std::string& xml, const std::string& name = "");

}  // namespace pittore::vector