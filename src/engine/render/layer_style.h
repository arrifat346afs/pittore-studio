#pragma once
// Per-layer effects ("layer styles"): drop and inner shadow, outer and inner
// glow, colour and gradient overlay, stroke, bevel and layer blur. Every one
// of them derives from the layer's own alpha — shadows and glows are that alpha
// blurred, offset and coloured; a stroke is a band along its edge; a bevel
// shades it by the slope of a blurred copy — so a style needs only the layer's
// straight-alpha RGBA pixels plus the parameters below.
//
// Both the .af decoder (which bakes a file's `FiEf` effects at import) and
// the editor's Layer Style dialog render through this one implementation.

#include <cstdint>
#include <vector>

namespace pittore::render {

struct StyleColor {
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
};

// The separable blend modes a style can name; anything else is Normal.
enum class StyleBlend {
    Normal, Multiply, Screen, Overlay, Darken, Lighten, ColorBurn, ColorDodge,
    HardLight, SoftLight, Difference, Exclusion, Add, Subtract
};

struct BlurStyle {
    float radius = 5.0f;
    bool preserveAlpha = false;
};

struct ShadowStyle {
    StyleColor color{0.0f, 0.0f, 0.0f, 1.0f};
    StyleBlend blend = StyleBlend::Multiply;
    float opacity = 0.75f;
    float angle = 120.0f;
    float distance = 5.0f;
    float spread = 0.0f;
    float size = 5.0f;
    bool knockout = true;
};

struct GlowStyle {
    StyleColor color{1.0f, 1.0f, 0.75f, 1.0f};
    StyleBlend blend = StyleBlend::Screen;
    float opacity = 0.75f;
    float spread = 0.0f;
    float size = 5.0f;
};

// The layer's own shape blurred and offset both ways, differenced against
// itself: the conventional "interior shading".
struct SatinStyle {
    StyleColor color{0.0f, 0.0f, 0.0f, 1.0f};
    StyleBlend blend = StyleBlend::Multiply;
    float opacity = 0.5f;
    float angle = 19.0f;
    float distance = 11.0f;
    float size = 14.0f;
    bool invert = true;
};

struct ColorOverlayStyle {
    StyleColor color{1.0f, 0.0f, 0.0f, 1.0f};
    StyleBlend blend = StyleBlend::Normal;
    float opacity = 1.0f;
};

struct StrokeStyle {
    StyleColor color{0.0f, 0.0f, 0.0f, 1.0f};
    StyleBlend blend = StyleBlend::Normal;
    float opacity = 1.0f;
    float size = 3.0f;
    int position = 0;  // 0 outside, 1 centre, 2 inside
};

struct BevelStyle {
    int style = 1;  // 0 outer, 1 inner, 2 emboss, 3 pillow
    float angle = 120.0f;
    float altitude = 30.0f;
    float size = 5.0f;
    float soften = 0.0f;
    float depth = 1.0f;
    StyleColor highlight{1.0f, 1.0f, 1.0f, 1.0f};
    StyleBlend highlightBlend = StyleBlend::Screen;
    float highlightOpacity = 0.75f;
    StyleColor shadow{0.0f, 0.0f, 0.0f, 1.0f};
    StyleBlend shadowBlend = StyleBlend::Multiply;
    float shadowOpacity = 0.75f;
};

struct GradientStyle {
    StyleColor from{0.0f, 0.0f, 0.0f, 1.0f};
    StyleColor to{1.0f, 1.0f, 1.0f, 1.0f};
    StyleBlend blend = StyleBlend::Normal;
    float opacity = 1.0f;
    float angle = 90.0f;
    bool radial = false;
    bool reverse = false;
    float scale = 1.0f;
};

struct LayerStyle {
    bool hasBlur = false;
    BlurStyle blur;
    bool hasDropShadow = false;
    ShadowStyle dropShadow;
    bool hasInnerShadow = false;
    ShadowStyle innerShadow;
    bool hasOuterGlow = false;
    GlowStyle outerGlow;
    bool hasInnerGlow = false;
    GlowStyle innerGlow;
    bool hasColorOverlay = false;
    ColorOverlayStyle colorOverlay;
    bool hasSatin = false;
    SatinStyle satin;
    bool hasStroke = false;
    StrokeStyle stroke;
    bool hasBevel = false;
    BevelStyle bevel;
    bool hasGradient = false;
    GradientStyle gradient;

    bool empty() const;
    // How far outside the content the effects reach, so the styled raster is
    // grown enough not to clip.
    int outset() const;
};

// Straight-alpha RGBA8 pixels.
struct Rgba8Image {
    std::uint32_t w = 0;
    std::uint32_t h = 0;
    std::vector<std::uint8_t> px;
};

// Render `style` over `img`. The image is grown by style.outset() on every side
// and `grow` receives that pad. Returns false — leaving `img` untouched — when
// the grown raster would exceed the internal pixel cap.
bool applyLayerStyle(Rgba8Image& img, const LayerStyle& style, int* grow = nullptr);

}  // namespace pittore::render
