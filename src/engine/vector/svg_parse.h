#pragma once
#include <string>

#include "engine/vector/vector_scene.h"

namespace pittore::vector {

// Parse an SVG document into a flat, coalesced VectorScene.
// Supported: viewport (width/height or viewBox), defs/linearGradient/
// radialGradient/stop (stop-color + stop-opacity, as attrs or style), rect,
// circle (fill + stroke), text (approximated as a filled box), hex + a small
// set of named colors, %-lengths, element opacity, url(#id) fills.
// Consecutive identical fully-opaque primitives are coalesced (source-over
// exact); the pre-coalesce element count is kept in raw_prim_count.
VectorScene parse_svg(const std::string& xml, const std::string& name);

}  // namespace pittore::vector