#pragma once
// Complete tool inventory (2026). Data-driven: everything
// downstream reads this table, so adding a tool is a one-line change.
// Split from ui/tool_registry.h — include ui/tool_registry.h for the full API.

namespace pittore::ui {

enum class ToolId {
    Move, Artboard,
    RectMarquee, EllipseMarquee, SingleRowMarquee, SingleColumnMarquee,
    Lasso, PolygonalLasso, MagneticLasso, SelectionBrush,
    QuickSelection, ObjectSelection, MagicWand,
    Crop, PerspectiveCrop, Slice, SliceSelect,
    Frame,
    Eyedropper, ColorSampler, Ruler, Note, Count,
    SpotHealing, Remove, HealingBrush, Patch, ContentAwareMove, RedEye,
    CloneStamp, PatternStamp,
    Eraser, BackgroundEraser, MagicEraser,
    Blur, Sharpen, Smudge,
    Liquify,
    Dodge, Burn, Sponge,
    Brush, Pencil, ColorReplacement, MixerBrush,
    HistoryBrush, ArtHistoryBrush,
    Gradient, PaintBucket,
    AdjustmentBrush,
    Pen, FreeformPen, CurvaturePen, ContentAwareTracing,
    AddAnchorPoint, DeleteAnchorPoint, ConvertPoint,
    PathSelection, DirectSelection,
    // Vector persona tools (vector-editor parity, vector.md §3).
    NodeTool, PointTransformTool,
    CornerTool, ContourTool, StrokeWidthTool, KnifeTool,
    VectorBrushTool, VectorFloodFillTool, ShapeBuilderTool,
    TransparencyTool, StylePickerTool, MeasureTool, AreaTool,
    VectorCropTool, PlaceTool,
    Rectangle, Ellipse, Triangle, Polygon, Star, Line, CustomShape,
    RoundedRectangle, Diamond, Trapezoid, DoubleStar, SquareStar, Arrow,
    Donut, Pie, Segment, Crescent, Cog, Cloud, CalloutRect, CalloutEllipse,
    Tear, Heart, Spiral, QRCode, Cat,
    // Beyond the baseline set: diagram and icon staples (badges, breadcrumbs, arrows).
    Hexagon, Octagon, Cross, RightTriangle, Parallelogram, Chevron,
    DoubleArrow, CircularArrow, Sparkle, Shield, Ticket, Sun,
    HorizontalType, VerticalType, HorizontalTypeMask, VerticalTypeMask,
    Hand, RotateView, Zoom,
    ImportPhotos, GenerativeFill, GenerateBackground,
    // Extended vector toolset for the vector engine.
    CalligraphyTool, SprayTool, MeshTool, ConnectorTool, TweakTool,
    Box3DTool, PagesTool, LpeTool, MarkerTool, VectorEraserTool,

    Count_,
};

}  // namespace pittore::ui
