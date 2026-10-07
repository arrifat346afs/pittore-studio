#include "ui/tools/options/section_options.h"

#include "ui/tools/options/option_builders.h"

namespace pittore::ui::detail {

std::vector<OptionSpec> navGenToolOptions(ToolId id) {
    switch (id) {
        case ToolId::Hand:
            return {check("scroll_all", "Scroll All Windows"), sep(),
                    button("fit", "Fit Screen"),
                    button("fill", "Fill Screen"),
                    button("actual", "100%")};

        case ToolId::RotateView:
            return {spin("angle", "Rotation Angle", -360, 360, 0, "°"),
                    button("reset", "Reset View"),
                    check("rotate_all", "Rotate All Windows")};

        case ToolId::Zoom:
            return {toggles("zoommode", {"Zoom In", "Zoom Out"}, 0), sep(),
                    check("resize_windows", "Resize Windows to Fit"),
                    check("zoom_all", "Zoom All Windows"),
                    check("scrubby", "Scrubby Zoom", true), sep(),
                    button("actual", "100%"),
                    button("fit", "Fit Screen"),
                    button("fill", "Fill Screen")};

        case ToolId::ImportPhotos:
            return {label("Import photos from a connected device."),
                    button("browse", "Choose Device…")};

        case ToolId::GenerativeFill:
            return {textField("prompt", "Prompt", "", 280),
                    combo("count", "Variations", {"1", "3", "5"}, 1, 90),
                    button("generate", "Generate")};

        case ToolId::GenerateBackground:
            return {textField("prompt", "Background", "", 280),
                    button("generate", "Generate")};
        default:
            return {};
    }
}

}  // namespace pittore::ui::detail
