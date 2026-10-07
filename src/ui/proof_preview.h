#pragma once
// Soft-proof rendering for the canvas: an 8-bit image through a configured
// ProofManager, back to a drawable image. Depends only on QtGui and the
// color engine (no canvas), so the conversion is unit-testable.

#include <QImage>

namespace pittore::color {
class ProofManager;
}

namespace pittore::ui {

// Rendered soft-proof of `src` through `pm` (document floats in,
// display floats out). Returns null when `src` is null or the proof fails,
// so the caller falls back to the unproofed blit. `pm` is single-threaded
// by contract — call only from the thread that configured it (the GUI
// thread on the canvas path).
QImage proofPreviewImage(const QImage& src, pittore::color::ProofManager& pm);

}  // namespace pittore::ui
