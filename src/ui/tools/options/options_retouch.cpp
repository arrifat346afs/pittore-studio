#include "ui/tools/options/section_options.h"

#include "ui/tools/options/option_builders.h"

namespace pittore::ui::detail {

std::vector<OptionSpec> retouchToolOptions(ToolId id) {
    switch (id) {
        case ToolId::SpotHealing:
            return {brushPreset(),
                    combo("mode", "Mode", blendModeItems(), 0, 130),
                    toggles("type", {"Content-Aware", "Create Texture", "Proximity Match"}, 0),
                    check("sample_all", "Sample All Layers"),
                    spin("diffusion", "Diffusion", 1, 7, 5)};

        case ToolId::Remove:
            return {brushPreset(),
                    check("remove_after_stroke", "Remove after each stroke", true),
                    check("sample_all", "Sample All Layers", true), sep(),
                    button("find_distractions", "Find Distractions…"),
                    combo("distraction", "", {"People", "Wires and Cables"}, 0, 140), sep(),
                    check("generative", "Generative (on-device model)", true)};

        case ToolId::HealingBrush:
            return {brushPreset(),
                    combo("mode", "Mode", blendModeItems(), 0, 130),
                    toggles("source", {"Sampled", "Pattern"}, 0),
                    check("aligned", "Aligned", true),
                    combo("sample", "Sample", {"Current Layer", "Current & Below", "All Layers"}, 0, 150),
                    spin("diffusion", "Diffusion", 1, 7, 5)};

        case ToolId::Patch:
            return {selectionModes(), sep(),
                    combo("patchmode", "Patch", {"Normal", "Content-Aware"}, 1, 130),
                    spin("structure", "Structure", 1, 7, 4),
                    spin("colorblend", "Color", 0, 10, 0),
                    check("sample_all", "Sample All Layers"),
                    check("transparent", "Transparent"),
                    button("use_pattern", "Use Pattern")};

        case ToolId::ContentAwareMove:
            return {selectionModes(), sep(),
                    combo("camode", "Mode", {"Move", "Extend"}, 0, 100),
                    spin("structure", "Structure", 1, 7, 4),
                    spin("colorblend", "Color", 0, 10, 0),
                    check("sample_all", "Sample All Layers", true),
                    check("transform_on_drop", "Transform On Drop", true)};

        case ToolId::RedEye:
            return {spin("pupilsize", "Pupil Size", 0, 100, 50, "%"),
                    spin("darken", "Darken Amount", 0, 100, 50, "%")};

        case ToolId::CloneStamp:
            return {brushPreset(),
                    combo("mode", "Mode", blendModeItems(), 0, 130),
                    spin("opacity", "Opacity", 1, 100, 100, "%"),
                    spin("flow", "Flow", 1, 100, 100, "%"),
                    check("airbrush", "Airbrush", false),
                    spin("airbrush_rate", "Rate", 1, 100, 20, ""), sep(),
                    check("aligned", "Aligned", true),
                    combo("sample", "Sample", {"Current Layer", "Current & Below", "All Layers"}, 0, 150),
                    check("ignore_adjustments", "Ignore Adjustment Layers"),
                    spin("angle", "Angle", -360, 360, 0, "°")};

        case ToolId::PatternStamp:
            return {brushPreset(),
                    combo("mode", "Mode", blendModeItems(), 0, 130),
                    spin("opacity", "Opacity", 1, 100, 100, "%"),
                    spin("flow", "Flow", 1, 100, 100, "%"), sep(),
                    combo("pattern", "Pattern", {"Bubbles", "Wrinkles", "Woven", "Herringbone"}, 0, 130),
                    check("aligned", "Aligned", true),
                    check("impressionist", "Impressionist")};

        case ToolId::Eraser:
            return {brushPreset(),
                    combo("mode", "Mode", {"Brush", "Pencil", "Block"}, 0, 100),
                    spin("opacity", "Opacity", 1, 100, 100, "%"),
                    spin("flow", "Flow", 1, 100, 100, "%"),
                    check("airbrush", "Airbrush", false),
                    spin("airbrush_rate", "Rate", 1, 100, 20, ""),
                    spin("smoothing", "Smoothing", 0, 100, 0, "%"), sep(),
                    check("erase_history", "Erase to History")};

        case ToolId::BackgroundEraser:
            return {brushPreset(),
                    toggles("sampling", {"Continuous", "Once", "Background Swatch"}, 0),
                    combo("limits", "Limits", {"Discontiguous", "Contiguous", "Find Edges"}, 1, 140),
                    spin("tolerance", "Tolerance", 1, 100, 50, "%"),
                    check("protect_fg", "Protect Foreground Color")};

        case ToolId::MagicEraser:
            return {spin("tolerance", "Tolerance", 0, 255, 32),
                    check("antialias", "Anti-alias", true),
                    check("contiguous", "Contiguous", true),
                    check("sample_all", "Sample All Layers"),
                    spin("opacity", "Opacity", 1, 100, 100, "%")};

        case ToolId::Blur:
        case ToolId::Sharpen:
            return {brushPreset(),
                    combo("mode", "Mode", {"Normal", "Darken", "Lighten", "Hue", "Saturation",
                                           "Color", "Luminosity"}, 0, 120),
                    spin("strength", "Strength", 1, 100, 50, "%"),
                    check("sample_all", "Sample All Layers"),
                    check("protect_detail", "Protect Detail", id == ToolId::Sharpen)};

        case ToolId::Smudge:
            return {brushPreset(),
                    combo("mode", "Mode", {"Normal", "Darken", "Lighten", "Hue", "Saturation",
                                           "Color", "Luminosity"}, 0, 120),
                    combo("smudge_mode", "Smudge", {"Dulling", "Smear"}, 0, 120),
                    spin("strength", "Strength", 1, 100, 50, "%"),
                    spin("smudge_color_rate", "Color", 0, 100, 0, "%"),
                    spin("smudge_length", "Trail", 0, 200, 100, "%"),
                    check("sample_all", "Sample All Layers"),
                    check("finger_paint", "Finger Painting")};

        case ToolId::Liquify:
            // The Liquify brush set (R48). The brush preset supplies
            // brush_size (read by CanvasView::strokeRadius); "pressure" is the
            // per-dab strength the brush preset calls it.
            return {brushPreset(),
                    combo("tool", "Tool",
                          {"Forward Warp", "Reconstruct", "Twirl CW", "Twirl CCW",
                           "Pucker", "Bloat", "Push Left"}, 0, 150),
                    spin("pressure", "Pressure", 1, 100, 50, "%")};

        case ToolId::Dodge:
        case ToolId::Burn:
            return {brushPreset(),
                    combo("range", "Range", {"Shadows", "Midtones", "Highlights"}, 1, 120),
                    spin("exposure", "Exposure", 1, 100, 50, "%"),
                    check("airbrush", "Airbrush", false),
                    spin("airbrush_rate", "Rate", 1, 100, 20, ""),
                    check("protect_tones", "Protect Tones", true)};

        case ToolId::Sponge:
            return {brushPreset(),
                    combo("spongemode", "Mode", {"Desaturate", "Saturate"}, 0, 120),
                    spin("flow", "Flow", 1, 100, 50, "%"),
                    check("airbrush", "Airbrush", false),
                    spin("airbrush_rate", "Rate", 1, 100, 20, ""),
                    check("vibrance", "Vibrance")};

        case ToolId::Brush:
            return paintingOptions(true, true, true);

        case ToolId::Pencil:
            return {brushPreset(),
                    combo("mode", "Mode", blendModeItems(), 0, 130),
                    spin("opacity", "Opacity", 1, 100, 100, "%"),
                    spin("smoothing", "Smoothing", 0, 100, 0, "%"),
                    check("auto_erase", "Auto Erase")};

        case ToolId::ColorReplacement:
            return {brushPreset(),
                    combo("mode", "Mode", {"Hue", "Saturation", "Color", "Luminosity"}, 2, 120),
                    toggles("sampling", {"Continuous", "Once", "Background Swatch"}, 0),
                    button("lock_area", "Lock Area"),
                    combo("limits", "Limits", {"Discontiguous", "Contiguous", "Find Edges"}, 1, 140),
                    spin("tolerance", "Tolerance", 1, 100, 30, "%"),
                    combo("sample_size", "Sample Size",
                          {"Point Sample", "3 x 3 Average", "5 x 5 Average",
                           "11 x 11 Average", "31 x 31 Average"}, 1, 150),
                    spin("harmony", "Harmony", 0, 100, 50, "%"),
                    check("antialias", "Anti-alias", true)};

        case ToolId::MixerBrush:
            return {brushPreset(), sep(),
                    combo("preset", "", {"Custom", "Dry", "Dry, Light Load", "Moist, Light Mix",
                                         "Wet, Light Mix", "Very Wet, Heavy Mix"}, 0, 170),
                    check("load_after", "Load brush after each stroke", true),
                    check("clean_after", "Clean brush after each stroke"),
                    spin("wet", "Wet", 0, 100, 50, "%"),
                    spin("load", "Load", 0, 100, 50, "%"),
                    spin("mix", "Mix", 0, 100, 50, "%"),
                    spin("flow", "Flow", 1, 100, 100, "%"),
                    check("sample_all", "Sample All Layers")};

        case ToolId::HistoryBrush:
            return {brushPreset(),
                    combo("mode", "Mode", blendModeItems(), 0, 130),
                    spin("opacity", "Opacity", 1, 100, 100, "%"),
                    spin("flow", "Flow", 1, 100, 100, "%"),
                    check("airbrush", "Airbrush", false),
                    spin("airbrush_rate", "Rate", 1, 100, 20, "")};

        case ToolId::ArtHistoryBrush:
            return {brushPreset(),
                    combo("mode", "Mode", blendModeItems(), 0, 130),
                    spin("opacity", "Opacity", 1, 100, 100, "%"),
                    combo("style", "Style", {"Tight Short", "Tight Medium", "Tight Long",
                                             "Loose Medium", "Loose Long", "Dab", "Tight Curl",
                                             "Loose Curl"}, 0, 140),
                    spin("area", "Area", 0, 500, 50, " px"),
                    spin("tolerance", "Tolerance", 0, 100, 0, "%")};

        case ToolId::Gradient:
            return {combo("preset", "Gradient", {"Foreground to Background", "Foreground to Transparent",
                                                 "Black, White", "Chrome", "Spectrum", "Copper"}, 0, 190),
                    toggles("gradtype", {"Linear", "Radial", "Angle", "Reflected", "Diamond"}, 0), sep(),
                    combo("mode", "Mode", blendModeItems(), 0, 130),
                    spin("opacity", "Opacity", 1, 100, 100, "%"),
                    combo("method", "Method", {"Perceptual", "Linear", "Classic"}, 0, 120), sep(),
                    check("reverse", "Reverse"),
                    check("dither", "Dither", true),
                    check("transparency", "Transparency", true)};

        case ToolId::PaintBucket:
            return {combo("fillsource", "", {"Foreground", "Pattern"}, 0, 120),
                    combo("mode", "Mode", blendModeItems(), 0, 130),
                    spin("opacity", "Opacity", 1, 100, 100, "%"),
                    spin("tolerance", "Tolerance", 0, 255, 32),
                    check("antialias", "Anti-alias", true),
                    check("contiguous", "Contiguous", true),
                    check("sample_all", "All Layers")};

        case ToolId::TransparencyTool:
            return {combo("transparency_type", "Type",
                           {"None", "Linear", "Elliptical", "Radial", "Conical"}, 1, 120), sep(),
                    check("reverse", "Reverse"),
                    check("maintain_aspect", "Maintain fill aspect ratio", true)};

        case ToolId::AdjustmentBrush:
            return {brushPreset(),
                    combo("adjustment", "Adjustment",
                          {"Brightness/Contrast", "Levels", "Curves", "Exposure", "Vibrance",
                           "Hue/Saturation", "Color Balance", "Black & White",
                           "Photo Filter", "Channel Mixer", "Color Lookup"}, 5, 170),
                    spin("opacity", "Opacity", 1, 100, 100, "%"),
                    spin("flow", "Flow", 1, 100, 100, "%"),
                    check("sample_all", "Sample All Layers", true),
                    check("auto_mask", "Auto-mask to subject", true)};
        default:
            return {};
    }
}

}  // namespace pittore::ui::detail
