#include "ui/tools/options/section_options.h"

#include "ui/tools/options/option_builders.h"

namespace pittore::ui::detail {

std::vector<OptionSpec> selectionToolOptions(ToolId id) {
    switch (id) {
        case ToolId::Move:
            return {check("autoselect", "Auto-Select", true),
                    combo("autoselect_scope", "", {"Layer", "Group"}, 0, 80),
                    check("showtransform", "Show Transform Controls", true), sep(),
                    toggles("align", {"Left", "Center H", "Right", "Top", "Center V", "Bottom"}),
                    sep(),
                    toggles("distribute", {"Distribute H", "Distribute V"}),
                    button("3dmode", "Align & Distribute…")};

        case ToolId::Artboard:
            return {combo("preset", "Size", {"Custom", "Letter", "A4", "iPhone 15 Pro",
                                             "iPad Pro 11\"", "Web 1920 × 1080", "Web 1366 × 768"}, 0, 150),
                    spin("w", "W", 1, 100000, 1920, " px"),
                    spin("h", "H", 1, 100000, 1080, " px"), sep(),
                    colorWell("artboardbg", "Background")};

        case ToolId::RectMarquee:
        case ToolId::EllipseMarquee:
            return {selectionModes(), sep(),
                    spin("feather", "Feather", 0, 1000, 0, " px"),
                    check("antialias", "Anti-alias", true), sep(),
                    combo("style", "Style", {"Normal", "Fixed Ratio", "Fixed Size"}, 0, 110),
                    spin("width", "W", 0, 100000, 0, " px"),
                    spin("height", "H", 0, 100000, 0, " px"), sep(),
                    button("select_subject", "Select Subject"),
                    button("select_sky", "Select Sky"),
                    button("refine", "Select and Mask…"),
                     button("enhance_edges", "Enhance Edges")};

        case ToolId::SingleRowMarquee:
        case ToolId::SingleColumnMarquee:
            return {selectionModes(), sep(), button("refine", "Select and Mask…"),
                     button("enhance_edges", "Enhance Edges")};

        case ToolId::Lasso:
            return {selectionModes(), sep(),
                    spin("feather", "Feather", 0, 1000, 0, " px"),
                    check("antialias", "Anti-alias", true), sep(),
                    button("select_subject", "Select Subject"),
                    button("refine", "Select and Mask…"),
                     button("enhance_edges", "Enhance Edges")};

        case ToolId::PolygonalLasso:
            return {selectionModes(), sep(),
                    spin("feather", "Feather", 0, 1000, 0, " px"),
                    check("antialias", "Anti-alias", true), sep(),
                    button("refine", "Select and Mask…"),
                     button("enhance_edges", "Enhance Edges")};

        case ToolId::MagneticLasso:
            return {selectionModes(), sep(),
                    spin("feather", "Feather", 0, 1000, 0, " px"),
                    check("antialias", "Anti-alias", true), sep(),
                    spin("width", "Width", 1, 256, 10, " px"),
                    spin("contrast", "Contrast", 1, 100, 10, "%"),
                    spin("frequency", "Frequency", 0, 100, 57), sep(),
                    check("pen_pressure", "Pen Pressure"),
                    button("refine", "Select and Mask…"),
                     button("enhance_edges", "Enhance Edges")};

        case ToolId::SelectionBrush:
            return {selectionModes(), sep(), brushPreset(),
                    spin("hardness", "Hardness", 0, 100, 100, "%"),
                    spin("opacity", "Opacity", 1, 100, 100, "%"),
                    spin("spacing", "Spacing", 1, 1000, 25, "%"), sep(),
                    combo("overlay", "Overlay", {"Marching Ants", "Overlay", "On Black", "On White"}, 1, 130),
                    colorWell("overlaycolor", "Colour"), sep(),
                    button("select_subject", "Select Subject"),
                    button("refine", "Select and Mask…"),
                     button("enhance_edges", "Enhance Edges")};

        case ToolId::QuickSelection:
            return {toggles("selmode", {"New", "Add", "Subtract"}, 1), sep(), brushPreset(),
                    spin("angle", "Angle", 0, 360, 0, "°"), sep(),
                    check("sample_all", "Sample All Layers"),
                    check("enhance_edge", "Enhance Edge", true), sep(),
                    button("select_subject", "Select Subject"),
                    button("refine", "Select and Mask…"),
                     button("enhance_edges", "Enhance Edges")};

        case ToolId::ObjectSelection:
            return {selectionModes(), sep(),
                    combo("objmode", "Mode", {"Rectangle", "Lasso"}, 0, 100),
                    check("object_finder", "Object Finder", true),
                    button("refresh_objects", "Refresh"), sep(),
                    check("sample_all", "Sample All Layers"),
                    check("hard_edge", "Hard Edge"), sep(),
                    button("select_subject", "Select Subject"),
                    button("refine", "Select and Mask…"),
                     button("enhance_edges", "Enhance Edges")};

        case ToolId::MagicWand:
            return {selectionModes(), sep(),
                    combo("sample_size", "Sample Size",
                          {"Point Sample", "3 by 3 Average", "5 by 5 Average", "11 by 11 Average",
                           "31 by 31 Average", "51 by 51 Average", "101 by 101 Average"}, 0, 150),
                    spin("tolerance", "Tolerance", 0, 255, 32),
                    check("antialias", "Anti-alias", true),
                    check("contiguous", "Contiguous", true),
                    check("sample_all", "Sample All Layers"), sep(),
                    button("select_subject", "Select Subject"),
                    button("refine", "Select and Mask…"),
                     button("enhance_edges", "Enhance Edges")};
        default:
            return {};
    }
}

}  // namespace pittore::ui::detail
