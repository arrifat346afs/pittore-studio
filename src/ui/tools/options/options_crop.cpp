#include "ui/tools/options/section_options.h"

#include "ui/tools/options/option_builders.h"

namespace pittore::ui::detail {

std::vector<OptionSpec> cropToolOptions(ToolId id) {
    switch (id) {
        case ToolId::Crop:
            return {combo("ratio", "Ratio", {"Ratio", "W × H × Resolution", "Original Ratio",
                                             "1:1 Square", "4:5 (8:10)", "5:7", "2:3 (4:6)", "16:9"}, 0, 150),
                    spin("ratio_w", "", 0, 100000, 0),
                    spin("ratio_h", "", 0, 100000, 0),
                    button("swap_ratio", "⇄"),
                    button("clear_ratio", "Clear"), sep(),
                    button("straighten", "Straighten"),
                    combo("overlay", "Overlay", {"Rule of Thirds", "Grid", "Diagonal",
                                                 "Triangle", "Golden Ratio", "Golden Spiral",
                                                 "Never Show Overlay", "Always Show Overlay"}, 0, 150),
                    sep(),
                    check("delete_cropped", "Delete Cropped Pixels", true),
                    check("content_aware", "Content-Aware Fill")};

        case ToolId::PerspectiveCrop:
            return {spin("w", "W", 0, 100000, 0, " px"),
                    spin("h", "H", 0, 100000, 0, " px"),
                    spin("res", "Resolution", 1, 10000, 300, " px/in"),
                    button("front_image", "Front Image"),
                    button("clear", "Clear"), sep(),
                    check("show_grid", "Show Grid", true)};

        case ToolId::Slice:
            return {combo("style", "Style", {"Normal", "Fixed Aspect Ratio", "Fixed Size"}, 0, 150),
                    spin("w", "W", 0, 100000, 0),
                    spin("h", "H", 0, 100000, 0), sep(),
                    button("slices_from_guides", "Slices From Guides"),
                    button("export-slices", "Export Slices…")};

        case ToolId::SliceSelect:
            return {toggles("stack", {"Bring to Front", "Forward", "Backward", "Send to Back"}), sep(),
                    button("promote", "Promote"),
                    button("divide", "Divide…"), sep(),
                    toggles("align", {"Left", "Center H", "Right", "Top", "Center V", "Bottom"}),
                    button("hide_autoslices", "Hide Auto Slices"),
                    button("export-slices", "Export Slices…")};

        case ToolId::Frame:
            return {toggles("frameshape", {"Rectangular", "Elliptical"}, 0), sep(),
                    button("insert_image", "Insert Image From…")};

        case ToolId::Eyedropper:
            return {combo("sample_size", "Sample Size",
                          {"Point Sample", "3 by 3 Average", "5 by 5 Average", "11 by 11 Average",
                           "31 by 31 Average", "51 by 51 Average", "101 by 101 Average"}, 0, 150),
                    combo("sample", "Sample",
                          {"All Layers", "Current Layer", "Current & Below",
                           "All Layers no Adjustments", "Current Layer no Adjustments"}, 0, 190),
                    check("sample_ring", "Show Sampling Ring", true)};

        case ToolId::ColorSampler:
            return {combo("sample_size", "Sample Size",
                          {"Point Sample", "3 by 3 Average", "5 by 5 Average", "11 by 11 Average",
                           "31 by 31 Average", "51 by 51 Average", "101 by 101 Average"}, 2, 150),
                    button("clear", "Clear All")};

        case ToolId::Ruler:
            return {spin("x", "X", -1e6, 1e6, 0), spin("y", "Y", -1e6, 1e6, 0),
                    spin("w", "W", -1e6, 1e6, 0), spin("h", "H", -1e6, 1e6, 0),
                    spin("a", "A", -360, 360, 0, "°"),
                    spin("l1", "L1", 0, 1e6, 0), spin("l2", "L2", 0, 1e6, 0), sep(),
                    button("straighten", "Straighten Layer"),
                    button("clear", "Clear")};

        case ToolId::Note:
            return {textField("author", "Author", "", 160),
                    colorWell("notecolor", "Colour"),
                    button("clear", "Clear All")};

        case ToolId::Count:
            return {spin("count", "Count", 0, 1e6, 0),
                    spin("group", "Count Group", 1, 99, 1),
                    button("newgroup", "New"),
                    button("delete", "Delete"),
                    colorWell("countcolor", "Colour"),
                    spin("marker", "Marker Size", 1, 10, 3),
                    spin("labelsize", "Label Size", 8, 72, 12),
                    button("clear", "Clear")};
        default:
            return {};
    }
}

}  // namespace pittore::ui::detail
