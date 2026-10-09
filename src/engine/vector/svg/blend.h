#pragma once
// Blend math on linear channels.
namespace pittore::svg {

enum class Blend { Normal, Multiply, Screen, Darken, Lighten, Overlay, Difference };

void blendPx(Blend m, const float src[4], const float dst[4], float out[4]);

}  // namespace pittore::svg
