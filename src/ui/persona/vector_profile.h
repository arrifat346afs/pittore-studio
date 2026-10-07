#pragma once
// Variable-width stroke profiles (ui/persona): multiplier control points
// over normalized arclength, painted as filled outlines. Pure QtCore/QtGui.
#include <QPainterPath>

#include "engine/vector/path.h"

namespace pittore::vector {
struct ArtNode;
struct ArtPaint;
}  // namespace pittore::vector

namespace pittore::ui {

// Largest absolute stroke width the paint can draw (base × peak profile
// multiplier, or the base width when uniform). Drives trim margins.
double maxProfileWidth(const pittore::vector::ArtPaint& paint);

// The paint's profile as an engine WidthProfile (empty when uniform).
pittore::vector::WidthProfile
widthProfileFrom(const pittore::vector::ArtPaint& paint);

// Insert (or merge, within 0.02 in t) a multiplier control point, enabling
// the profile. The first point seeds {0:1, 1:1} so edits stay local.
void setProfilePoint(pittore::vector::ArtPaint& paint, float t, float mult);

// The profiled stroke as a node-space path (centerline expanded, dashed
// first when both apply). Empty when the paint draws a uniform stroke —
// callers keep their QPen fast path then. Fill with the stroke colour under
// Qt::WindingFill.
QPainterPath expandStrokePath(const pittore::vector::ArtNode& node);

}  // namespace pittore::ui
