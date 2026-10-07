#pragma once
// Gradient + transparency fills for retained vector art (ui/persona).
//
// The Gradient tool lays a preset colour gradient along the drag axis; the
// Transparency tool lays an alpha ramp (a transparency gradient)
// over the fill. Both work in node coordinates and return a replacement
// ArtPaint the caller commits with applyVectorPaint. Pure QtCore.
#include <QColor>
#include <QPointF>

#include <vector>

#include "engine/vector/vector_art.h"

namespace pittore::ui {

// Preset stop colours (bar's Gradient preset index): Foreground to
// Background, Foreground to Transparent, Black/White, Chrome, Spectrum,
// Copper. `reverse` swaps the ramp.
std::vector<pittore::vector::ArtStop> gradientPresetStops(
    int preset, const QColor& foreground, const QColor& background,
    bool reverse);

// Lay a linear (or radial, when `radial`) colour gradient along
// nodeStart→nodeEnd over `paint`. Enables fill; stroke untouched.
pittore::vector::ArtPaint paintWithGradient(
    const pittore::vector::ArtPaint& paint, const QPointF& nodeStart,
    const QPointF& nodeEnd, bool radial,
    const std::vector<pittore::vector::ArtStop>& stops);

// Lay an alpha ramp over the fill along nodeStart→nodeEnd. The base colour
// is the current fill (or `fallback` for stroke-only art, which gains a
// fill). `type`: 0 None (clears to flat), 1 Linear, 2 Elliptical (~radial),
// 3 Radial, 4 Conical (~linear). `reverse` flips the ramp.
pittore::vector::ArtPaint paintWithTransparency(
    const pittore::vector::ArtPaint& paint, const QPointF& nodeStart,
    const QPointF& nodeEnd, int type, bool reverse, const QColor& fallback);

}  // namespace pittore::ui
