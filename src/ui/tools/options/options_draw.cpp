#include "ui/tools/options/section_options.h"

#include "ui/tools/options/option_builders.h"

namespace pittore::ui::detail {

std::vector<OptionSpec> drawToolOptions(ToolId id) {
    switch (id) {
        case ToolId::Pen:
            return {combo("penmode", "", {"Path", "Shape"}, 0, 90), sep(),
                    combo("spline", "Curve",
                           {"Bezier", "Spiro", "B-Spline", "Straight", "Perpendicular"},
                           0, 130),
                    colorWell("fill", "Fill"),
                    colorWell("stroke", "Stroke"),
                    spin("strokewidth", "", 0, 1000, 1, " px", 0.5), sep(),
                    combo("pathop", "", {"New Layer", "Combine", "Subtract", "Intersect", "Exclude"}, 0, 110),
                    check("autoadd", "Auto Add/Delete", true),
                    check("rubberband", "Rubber Band")};

        case ToolId::FreeformPen:
            return {combo("penmode", "", {"Path", "Shape"}, 0, 90), sep(),
                    combo("spline", "Curve",
                           {"Bezier", "Spiro", "B-Spline", "Straight"}, 0, 130),
                    spin("simplify", "Simplify", 0, 20, 2.0, " px", 0.5),
                    check("magnetic", "Magnetic"),
                    spin("curvefit", "Curve Fit", 0.5, 10.0, 2.0, " px", 0.5),
                    spin("width", "Width", 1, 256, 10, " px"),
                    spin("contrast", "Contrast", 1, 100, 10, "%"),
                    spin("frequency", "Frequency", 0, 100, 57)};

        case ToolId::CurvaturePen:
            return {combo("penmode", "", {"Path", "Shape"}, 0, 90), sep(),
                    colorWell("fill", "Fill"),
                    colorWell("stroke", "Stroke"),
                    spin("strokewidth", "", 0, 1000, 1, " px", 0.5),
                    check("rubberband", "Rubber Band", true)};

        case ToolId::ContentAwareTracing:
            return {combo("output", "Output", {"Path", "Selection", "Shape"}, 0, 120),
                    spin("detail", "Detail", 0, 100, 50, "%"),
                    check("preview_edges", "Preview Edges", true),
                    check("close_path", "Close Path", true),
                    button("commit", "Commit")};

        case ToolId::AddAnchorPoint:
        case ToolId::DeleteAnchorPoint:
        case ToolId::ConvertPoint:
            return {label("Click a path to modify its anchor points.")};

        case ToolId::StrokeWidthTool:
            return {check("lock_weight", "Lock Line Weight", true),
                    check("lock_order", "Lock Point Reordering", true), sep(),
                    button("reset_profile", "Reset Profile")};

        case ToolId::KnifeTool:
            return {check("straight_line", "Straight Line"),
                    combo("auto_close", "Auto Close", {"Off", "Near", "Far", "Always"}, 0, 90),
                    spin("smoothness", "Smoothness", 0, 100, 50, "%")};

        case ToolId::NodeTool:
            return {colorWell("fill", "Fill"),
                    colorWell("stroke", "Stroke"),
                    spin("strokewidth", "", 0, 1000, 1, " px", 0.5), sep(),
                    combo("convert", "", {"Sharp", "Smooth", "Smart"}, 1, 90), sep(),
                    button("node_split", "Split"),
                    button("node_join", "Join"),
                    button("node_close", "Close"),
                    button("node_reverse", "Reverse")};

        case ToolId::PointTransformTool:
            return {colorWell("fill", "Fill"),
                    colorWell("stroke", "Stroke"),
                    spin("strokewidth", "", 0, 1000, 1, " px", 0.5), sep(),
                    check("hide_selection", "Hide Selection while Dragging")};

        case ToolId::CornerTool:
            return {colorWell("fill", "Fill"),
                    colorWell("stroke", "Stroke"),
                    spin("strokewidth", "", 0, 1000, 1, " px", 0.5), sep(),
                    combo("corner_type", "", {"None", "Rounded", "Straight", "Concave", "Cutout"}, 1, 100),
                    spin("radius", "Radius", 0, 10000, 0, " px"), sep(),
                    button("bake_appearance", "Bake Appearance")};

        case ToolId::ContourTool:
            return {colorWell("fill", "Fill"),
                    colorWell("stroke", "Stroke"),
                    spin("strokewidth", "", 0, 1000, 1, " px", 0.5), sep(),
                    spin("contour_radius", "Radius", -100000, 100000, 0, " px"),
                    combo("contour_type", "", {"Round", "Bevel", "Miter"}, 0, 90), sep(),
                    button("bake_appearance", "Bake Appearance")};

        case ToolId::PathSelection:
            return {combo("select", "Select", {"Active Layers", "All Layers"}, 0, 130), sep(),
                    colorWell("fill", "Fill"),
                    colorWell("stroke", "Stroke"),
                    spin("strokewidth", "", 0, 1000, 1, " px", 0.5), sep(),
                    combo("pathop", "", {"Combine", "Subtract", "Intersect", "Exclude", "Merge"}, 0, 110),
                    toggles("align", {"Left", "Center H", "Right", "Top", "Center V", "Bottom"}),
                    check("constrain", "Constrain Path Dragging", true)};

        case ToolId::DirectSelection:
            return {combo("select", "Select", {"Active Layers", "All Layers"}, 0, 130),
                    check("constrain", "Constrain Path Dragging", true)};

        case ToolId::Rectangle:
        case ToolId::RoundedRectangle:
            return shapeOptions(true, false, false, true);
        case ToolId::Ellipse:     return shapeOptions(false, false, false);
        case ToolId::Triangle:    return shapeOptions(true, false, false);
        case ToolId::Polygon:     return shapeOptions(true, true, false);
        case ToolId::Star:        return shapeOptions(true, true, true);
        case ToolId::CustomShape: {
            auto o = shapeOptions(false, false, false);
            o.push_back(sep());
            o.push_back(combo("customshape", "Shape",
                              {"Lightning", "Droplet", "Moon", "Plus", "Target",
                               "Frame"},
                              0, 110));
            return o;
        }
        case ToolId::Hexagon:
        case ToolId::Octagon:
        case ToolId::RightTriangle:
        case ToolId::Shield:
            return shapeOptions(true, false, false);
        case ToolId::Cross:
        case ToolId::Chevron:
        case ToolId::CircularArrow:
        case ToolId::Ticket:
            return shapeOptions(false, false, false);
        case ToolId::Parallelogram: {
            auto o = shapeOptions(false, false, false);
            o.push_back(sep());
            o.push_back(spin("skew", "Skew", -100, 100, 25, "%"));
            return o;
        }
        case ToolId::Sun:
            // Ray count rides the shared Sides control.
            return shapeOptions(false, true, false);
        case ToolId::Sparkle:
            return shapeOptions(true, true, true);
        case ToolId::Diamond:
        case ToolId::Trapezoid:
        case ToolId::Crescent:
        case ToolId::Cloud:
        case ToolId::CalloutRect:
        case ToolId::CalloutEllipse:
        case ToolId::Tear:
        case ToolId::Heart:
        case ToolId::Spiral:
        case ToolId::Cat:
            return shapeOptions(false, false, false);
        case ToolId::DoubleStar:
        case ToolId::SquareStar:
            return shapeOptions(true, true, true);
        case ToolId::Donut:
        case ToolId::Cog: {
            auto o = shapeOptions(false, true, false);
            o.push_back(sep());
            o.push_back(spin("hole", "Hole", 0, 100, 25, "%"));
            return o;
        }        case ToolId::Pie:
        case ToolId::Segment: {
            auto o = shapeOptions(false, false, false);
            o.push_back(sep());
            o.push_back(spin("start_angle", "Start", -360, 360, 0, "°"));
            o.push_back(spin("end_angle", "End", -360, 360, 90, "°"));
            return o;
        }
        case ToolId::QRCode:
            return {textField("qr_content", "Content", "https://", 200), sep(),
                    spin("qr_size", "Size", 21, 1024, 256, " px"),
                    combo("qr_ecc", "ECC",
                          {"Low", "Medium", "Quartile", "High"}, 0, 110)};

        case ToolId::Line:
        case ToolId::Arrow:
        case ToolId::DoubleArrow:
            return {combo("shapemode", "", {"Shape", "Path", "Pixels"}, 0, 90), sep(),
                    colorWell("fill", "Fill"),
                    colorWell("stroke", "Stroke"),
                    spin("weight", "Weight", 0.1, 1000, 1, " px", 0.5), sep(),
                    check("start_arrow", "Start Arrowhead"),
                    check("end_arrow", "End Arrowhead"),
                    spin("arrow_width", "Arrow W", 10, 1000, 500, "%"),
                    spin("arrow_length", "Arrow L", 10, 5000, 1000, "%")};

        case ToolId::HorizontalType:
        case ToolId::VerticalType:
        case ToolId::HorizontalTypeMask:
        case ToolId::VerticalTypeMask:
            return {button("orientation", "⇅"), sep(),
                    combo("family", "", familyItems(), 0, 160),
                    combo("style", "", {"Regular", "Italic", "Medium", "Semibold", "Bold", "Black",
                                        "Bold Italic"}, 0, 100),
                    spin("size", "", 1, 1296, 36, " pt", 0.5),
                    combo("antialias", "", {"None", "Sharp", "Crisp", "Strong", "Smooth"}, 3, 100), sep(),
                    toggles("align", {"Left", "Center", "Right"}, 0),
                    colorWell("color", "Colour"), sep(),
                    button("warp", "Create Warped Text"),
                    button("panels", "Character and Paragraph Panels")};

        case ToolId::VectorBrushTool:
            return {colorWell("color", "Colour"),
                    spin("brush_width", "Width", 0.1, 1000, 8, " px", 0.5),
                    spin("brush_opacity", "Opacity", 1, 100, 100, "%"), sep(),
                    combo("mode", "Blend", blendModeItems(), 0, 130),
                    combo("controller", "Controller",
                           {"Brush Defaults", "Automatic", "Pressure", "Velocity"}, 0, 130),
                    sep(),
                    spin("brush_angle", "Nib angle", -180, 180, 0, "°", 1),
                    spin("brush_roundness", "Nib roundness", 1, 100, 100, "%", 1),
                    combo("brush_tip", "Nib", {"Round", "Square"}, 0, 90)};

        case ToolId::VectorFloodFillTool:
            return {combo("insertion", "Insertion", {"Inside", "In-between"}, 0, 110),
                    combo("fill_mode", "Fill", {"Add on top", "Smart refill", "Knockout"}, 0, 120), sep(),
                    check("visible_bounds", "Fill to Visible Boundaries", true),
                    combo("fit", "Fit", {"Max", "Min", "Stretch", "None"}, 0, 90)};

        case ToolId::ShapeBuilderTool:
            return {toggles("builder_action", {"Add", "Delete", "Create"}, 0), sep(),
                    combo("drag_method", "Drag", {"Freehand", "Straight", "Marquee"}, 0, 110),
                    combo("cleanup", "Clean up",
                           {"None", "Internal curves", "Connected curves", "All unused"}, 0, 150),
                    check("first_style", "Use style from first area", true)};

        case ToolId::VectorCropTool:
            return {label("Drag the handles to crop. Drag inside to reposition content.")};

        case ToolId::PlaceTool:
            return {button("place_browse", "Place Image…"), sep(),
                    label("…or drop an image onto the canvas.")};

        case ToolId::StylePickerTool:
            return {label("Pick which attributes the Style Picker applies:"),
                    check("pick_stroke", "Stroke", true),
                    check("pick_fill", "Fill", true),
                    check("pick_opacity", "Layer Opacity", true),
                    check("pick_effects", "Layer Effects"), sep(),
                    button("style_unload", "Unload")};

        case ToolId::MeasureTool:
        case ToolId::AreaTool:
            return {combo("measure_units", "Units", {"px", "pt", "mm", "cm", "in"}, 0, 80),
                    check("assign_scale", "Assign drawing scale from measurement")};

        // Extra pen spline modes (Bezier/Spiro/B-Spline/
        // Straight) ride the Pen group; calligraphy/spray/mesh/connector/
        // tweak/box/pages/LPE/marker/eraser each get their engine-backed row.
        case ToolId::CalligraphyTool:
            return {spin("nib_width", "Width", 0.5, 200, 12, " px", 0.5),
                    spin("nib_angle", "Angle", -90, 90, 30, "°"),
                    spin("nib_flat", "Flatness", 1, 100, 15, "%"),
                    spin("nib_thin", "Thinning", -100, 100, 0, "%"),
                    spin("nib_mass", "Mass", 0, 100, 0, "%")};
        case ToolId::SprayTool:
            return {spin("spray_radius", "Radius", 1, 500, 40, " px"),
                    spin("spray_scatter", "Scatter", 0, 100, 100, "%"),
                    spin("spray_scale", "Scale", 10, 400, 100, "%"),
                    check("spray_clone", "Spray clones")};
        case ToolId::MeshTool:
            return {combo("mesh_mode", "Mode", {"Create", "Edit", "Pick"}, 1, 100),
                    check("mesh_conical", "Conical")};
        case ToolId::ConnectorTool:
            return {combo("conn_kind", "Kind", {"Straight", "Polyline", "Orthogonal"}, 2, 120),
                    check("conn_avoid", "Avoid obstacles", true)};
        case ToolId::TweakTool:
            return {combo("tweak_mode", "Mode",
                           {"Push", "Shrink", "Grow", "Roughen", "Color"}, 0, 120),
                    spin("tweak_force", "Force", 1, 100, 30, "%")};
        case ToolId::Box3DTool:
            return {check("box_2pt", "Two-point perspective", true),
                    spin("box_depth", "Depth", -500, 500, -40, " px")};
        case ToolId::PagesTool:
            return {button("page_add", "New Page"), sep(),
                    combo("page_size", "Size", {"A4", "A3", "Letter", "Custom"}, 0, 100)};
        case ToolId::LpeTool: {
            return {combo("lpe_effect", "Effect",
                           {"Offset", "Fillet / chamfer", "Simplify", "Bend path",
                            "Envelope", "Roughen", "Sketch", "Taper stroke",
                            "Power stroke", "Rotate copies", "Mirror symmetry",
                            "Tiling", "Gears", "VonKoch", "Extrude", "Measure",
                            "PowerClip"},
                           0, 130),
                    spin("lpe_amount", "Amount", 0, 500, 30, "%"), sep(),
                    button("lpe_open", "Path Effects Panel")};
        }
        case ToolId::MarkerTool:
            return {combo("marker_pos", "Apply to", {"Start", "Mid", "End", "All"}, 3, 100),
                    spin("marker_scale", "Scale", 10, 1000, 100, "%")};
        case ToolId::VectorEraserTool:
            return {spin("veraser_width", "Width", 1, 200, 12, " px"),
                    combo("veraser_mode", "Mode", {"Trim", "Delete"}, 0, 100)};

        default:
            return {};
    }
}

}  // namespace pittore::ui::detail
