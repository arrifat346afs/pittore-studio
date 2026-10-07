#pragma once
#include <cstdint>
#include <utility>
#include <vector>

#include "engine/render/layer_style.h"

namespace pittore::render {
namespace detail {

std::vector<float> offsetAlpha(const std::vector<float>& alpha, int w, int h, float dx, float dy);
void applySpread(std::vector<float>& a, float spread);
float smoothBand(float d, float lo, float hi);
std::pair<float, float> polar(float angleDeg, float distance);
void shadowPlane(Plane& out, const std::vector<float>& alpha, int w, int h,
                   const Rect& rect, const ShadowStyle& s, bool inner);
void glowPlane(Plane& out, const std::vector<float>& alpha, int w, int h, const Rect& rect,
                 const GlowStyle& g, bool inner);
void satinPlane(Plane& out, const std::vector<float>& alpha, int w, int h, const Rect& rect,
                const SatinStyle& s);
void strokePlane(Plane& out, const std::vector<float>& alpha, int w, int h, const Rect& rect,
                   const StrokeStyle& s);
void bevelPlane(Plane& out, const std::vector<float>& alpha, int w, int h, const Rect& rect,
                  const BevelStyle& b);
void gradientPlane(Plane& out, const Rect& rect, const GradientStyle& o,
                     const Rect& content);
void compositeBehind(Plane& dst, const Plane& src, float opacity,
                       const std::vector<float>* knockout);
void blurContent(Plane& base, std::vector<float>& alpha, int w, int h, const BlurStyle& b);

}  // namespace detail
}  // namespace pittore::render
