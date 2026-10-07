#include "ui/persona/persona.h"

#include <algorithm>

#include "ui/tool_registry.h"

namespace pittore::ui {

QString personaName(Persona p) {
    switch (p) {
        case Persona::Vector:
            return QObject::tr("Vector");
        case Persona::Draw:
            return QObject::tr("Draw");
        case Persona::Color:
            return QObject::tr("Color");
        default:
            break;
    }
    return QObject::tr("Pixel");
}

QString personaId(Persona p) {
    switch (p) {
        case Persona::Vector:
            return QStringLiteral("vector");
        case Persona::Draw:
            return QStringLiteral("draw");
        case Persona::Color:
            return QStringLiteral("color");
        default:
            break;
    }
    return QStringLiteral("pixel");
}

bool personaFromId(const QString& id, Persona* out) {
    if (id == QStringLiteral("vector")) {
        if (out) *out = Persona::Vector;
        return true;
    }
    if (id == QStringLiteral("pixel")) {
        if (out) *out = Persona::Pixel;
        return true;
    }
    if (id == QStringLiteral("draw")) {
        if (out) *out = Persona::Draw;
        return true;
    }
    if (id == QStringLiteral("color")) {
        if (out) *out = Persona::Color;
        return true;
    }
    return false;
}

std::vector<ToolId> vectorVisibleLeaders() {
    // Group leaders shown in the Vector tab (vector.md §6.2). Artboard rides
    // with Move (same group); ColorSampler/Ruler/Note/Count/StylePicker/
    // Measure/Area ride with Eyedropper; StrokeWidth/Knife ride with the
    // anchor group; PointTransform rides with Node; Contour rides with
    // Corner; ShapeBuilder rides with Flood Fill; Transparency rides with
    // Gradient.
    return {
        ToolId::Move,
        ToolId::Eyedropper,
        ToolId::Gradient,
        ToolId::Pen,
        ToolId::AddAnchorPoint,
        ToolId::PathSelection,
        ToolId::NodeTool,
        ToolId::CornerTool,
        ToolId::Rectangle,
        ToolId::VectorBrushTool,
        ToolId::VectorFloodFillTool,
        ToolId::VectorCropTool,
        ToolId::PlaceTool,
        ToolId::CalligraphyTool,
        ToolId::SprayTool,
        ToolId::MeshTool,
        ToolId::ConnectorTool,
        ToolId::TweakTool,
        ToolId::Box3DTool,
        ToolId::PagesTool,
        ToolId::LpeTool,
        ToolId::MarkerTool,
        ToolId::VectorEraserTool,
        ToolId::HorizontalType,
        ToolId::Hand,
        ToolId::Zoom,
    };
}

std::vector<ToolId> vectorHiddenLeaders() {
    const std::vector<ToolId> visible = vectorVisibleLeaders();
    std::vector<ToolId> out;
    for (const ToolGroup& group : allGroups()) {
        if (std::find(visible.begin(), visible.end(), group.leader) == visible.end())
            out.push_back(group.leader);
    }
    return out;
}

QStringList hiddenToolsFor(Persona p) {
    std::vector<ToolId> hidden;
    if (p == Persona::Vector)
        hidden = vectorHiddenLeaders();
    else if (p == Persona::Draw)
        hidden = drawHiddenLeaders();
    else if (p == Persona::Color)
        hidden = colorHiddenLeaders();
    else
        hidden = pixelHiddenLeaders();
    QStringList out;
    for (ToolId id : hidden)
        out << QString::number(static_cast<int>(id));
    return out;
}

// Vector-exclusive leaders: the Vector tab's list minus the utilities it
// shares (Move/Eyedropper/Gradient/Hand/Zoom show in every persona). Text is
// shared, not vector-exclusive: live text layers hit-test and edit from any
// tab, so the Type group stays visible in Pixel too.
std::vector<ToolId> pixelHiddenLeaders() {
    return {
        ToolId::Pen,
        ToolId::AddAnchorPoint,
        ToolId::PathSelection,
        ToolId::NodeTool,
        ToolId::CornerTool,
        ToolId::Rectangle,
        ToolId::VectorBrushTool,
        ToolId::VectorFloodFillTool,
        ToolId::VectorCropTool,
        ToolId::PlaceTool,
    };
}

// Paint family + shared utilities. Smudge rides with Blur, Sponge with
// Dodge, Pencil/Mixer/ColorReplacement with Brush, PaintBucket with
// Gradient, Transparency rides with Gradient into both Vector and Draw.
std::vector<ToolId> drawVisibleLeaders() {
    return {
        ToolId::Move,       ToolId::Brush,      ToolId::Eraser,
        ToolId::Blur,       ToolId::Dodge,      ToolId::HistoryBrush,
        ToolId::Gradient,   ToolId::Eyedropper, ToolId::Hand,
        ToolId::Zoom,
    };
}

std::vector<ToolId> drawHiddenLeaders() {
    const std::vector<ToolId> visible = drawVisibleLeaders();
    std::vector<ToolId> out;
    for (const ToolGroup& group : allGroups()) {
        if (std::find(visible.begin(), visible.end(), group.leader) == visible.end())
            out.push_back(group.leader);
    }
    return out;
}

QStringList vectorPanels() {
    // stroke/appearance are both registered (Phase 2).
    // character/paragraph are floating: listed so the tab knows them, but the
    // manager never auto-shows floating panels (separate windows uninvited).
    return {QStringLiteral("color"), QStringLiteral("swatches"),
            QStringLiteral("stroke"), QStringLiteral("appearance"),
            QStringLiteral("layers"),
            QStringLiteral("paths"), QStringLiteral("properties"),
            QStringLiteral("brushes"), QStringLiteral("character"),
            QStringLiteral("paragraph"), QStringLiteral("navigator"),
            QStringLiteral("history")};
}

// Vector-exclusive panels (never shown outside Vector).
QStringList vectorOnlyPanels() {
    return {QStringLiteral("stroke"), QStringLiteral("appearance"),
            QStringLiteral("paths"), QStringLiteral("character"),
            QStringLiteral("paragraph")};
}

QStringList pixelOnlyPanels() {
    return {QStringLiteral("channels"), QStringLiteral("adjustments"),
            QStringLiteral("histogram"), QStringLiteral("actions")};
}

QStringList pixelFallbackPanels() {
    // Mirrors the Essentials preset in workspace.cpp.
    return {QStringLiteral("color"), QStringLiteral("swatches"),
            QStringLiteral("properties"), QStringLiteral("adjustments"),
            QStringLiteral("layers"), QStringLiteral("channels"),
            QStringLiteral("paths")};
}

// Canvas-first paint layout: the Painting workspace plus the navigator and
// the live brush preview.
QStringList drawPanels() {
    return {QStringLiteral("brushes"), QStringLiteral("brushpreview"),
            QStringLiteral("color"), QStringLiteral("swatches"),
            QStringLiteral("layers"), QStringLiteral("navigator"),
            QStringLiteral("history")};
}

// Grade-only tools: sampling is grading's input (Eyedropper group hosts the
// ColorSampler pins + Ruler/Note/Count ride free); Dodge hosts Burn/Sponge
// (tone-mapping heritage, all paint-gated on a pixel layer); Gradient hosts
// PaintBucket + AdjustmentBrush (the drawn-mask entry — the Color tab's mask
// story) for gradient-map/vignette looks; Move/Hand/Zoom are shared utilities.
std::vector<ToolId> colorVisibleLeaders() {
    return {
        ToolId::Move,       ToolId::Eyedropper, ToolId::Dodge,
        ToolId::Gradient,   ToolId::Hand,       ToolId::Zoom,
    };
}

std::vector<ToolId> colorHiddenLeaders() {
    const std::vector<ToolId> visible = colorVisibleLeaders();
    std::vector<ToolId> out;
    for (const ToolGroup& group : allGroups()) {
        if (std::find(visible.begin(), visible.end(), group.leader) == visible.end())
            out.push_back(group.leader);
    }
    return out;
}

// Scopes-first: histogram/info input, adjustments/properties grade,
// layers/channels context. Show order = dock top-to-bottom in the manager.
QStringList colorPanels() {
    return {QStringLiteral("histogram"), QStringLiteral("info"),
            QStringLiteral("color"), QStringLiteral("swatches"),
            QStringLiteral("adjustments"), QStringLiteral("properties"),
            QStringLiteral("layers"), QStringLiteral("channels")};
}

QStringList colorOnlyPanels() {
    return {};
}

QStringList colorFallbackPanels() {
    return colorPanels();
}

}  // namespace pittore::ui
