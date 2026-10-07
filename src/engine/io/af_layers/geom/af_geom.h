#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "engine/io/af_layers.h"
#include "engine/render/layer_style.h"
#include "engine/text/text_engine.h"
#include "engine/vector/vector_art.h"
#include "engine/vector/vector_shape.h"

namespace pittore::io {
namespace af_detail {

struct Mat {
    double m[6];
};

struct IntRect {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    int width() const { return x1 - x0; }
    int height() const { return y1 - y0; }
    bool empty() const { return x1 <= x0 || y1 <= y0; }
};

Mat translation(double tx, double ty);
Mat matThen(const Mat& a, const Mat& b);
std::pair<double, double> matApply(const Mat& m, double x, double y);
bool matAxisAligned(const Mat& m);
bool matInvert(const Mat& m, Mat& out);
std::uint8_t byteClamp(double v);
double roundTiesEven(double v);
std::int64_t floorDiv(std::int64_t a, std::int64_t b);
double matScaleX(const Mat& m);
double matScaleY(const Mat& m);

}  // namespace af_detail
}  // namespace pittore::io
