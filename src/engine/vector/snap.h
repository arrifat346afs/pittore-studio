#pragma once
// Snapping engine: candidate generation + best-target selection.
//
// Grid, guide, alignment and distribution targets form a bitmask so the UI's
// "Snap to {...}" checkboxes map 1:1. The engine is toolkit-free: callers
// feed document-space candidates; snapPoint returns the snapped point plus
// which target won (for the indicator text that no longer flickers when bbox
// and path coincide -- ties break toward the tighter class).
#include <functional>
#include <optional>
#include <vector>

namespace pittore::vector {

enum SnapTarget : unsigned {
    SnapNone = 0,
    SnapGrid = 1 << 0,
    SnapGuide = 1 << 1,
    SnapNode = 1 << 2,
    SnapPath = 1 << 3,
    SnapIntersection = 1 << 4,
    SnapBbox = 1 << 5,
    SnapMidpoint = 1 << 6,
    SnapCenter = 1 << 7,
    SnapAll = 0xFF,
};

struct SnapCandidate {
    double x = 0.0, y = 0.0;
    unsigned target = SnapNode;  // one bit from SnapTarget
    double weight = 1.0;         // tie-break multiplier (<1 wins ties)
};

struct SnapResult {
    double x = 0.0, y = 0.0;
    unsigned target = SnapNone;
    double distance = 0.0;
    bool snapped = false;
};

// Snap `p` to the nearest candidate within `tolerance` (document units).
// `enabled` masks which target bits participate.
SnapResult snapPoint(double px, double py, const std::vector<SnapCandidate>& cands,
                     double tolerance, unsigned enabled = SnapAll);

// Builders shared by canvas code -------------------------------------------
// Grid lattice around `p` (spacing `step`, origin `ox,oy`).
std::vector<SnapCandidate> gridCandidates(double px, double py, double step, double ox = 0,
                                          double oy = 0);
// Guide lines (positions along each axis).
std::vector<SnapCandidate> guideCandidates(double px, double py,
                                            const std::vector<double>& vGuides,
                                            const std::vector<double>& hGuides);
// Every anchor + handle of a VectorPath-like anchor list.
std::vector<SnapCandidate> nodeCandidates(
    const std::vector<std::pair<double, double>>& anchors);
// Bbox corners/edge-midpoints/center of a rect.
std::vector<SnapCandidate> bboxCandidates(double x0, double y0, double x1, double y1);
// Segment intersections between two polylines (tolerance-merged).
std::vector<SnapCandidate> intersectionCandidates(
    const std::vector<std::pair<double, double>>& a,
    const std::vector<std::pair<double, double>>& b, double tolerance = 1e-6);

}  // namespace pittore::vector
