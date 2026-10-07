#pragma once
// Shared SAM-decoder prompt runner used by the public decode entry points.
// Split out so the runner can be shared without dragging the entry points
// into every consumer.
#include <string>
#include <vector>

#include "engine/ai/bg_remove.h"
#include "engine/ai/shared/ai_session.h"

namespace pittore::ai {
namespace ai_detail {

SamDecodeResult decodePrompt(const SamEncodings& enc,
                             const std::vector<float>& coordsPx,
                             const std::vector<float>& labels,
                             const float* promptMask,
                             const std::string& decoderPath);

}  // namespace ai_detail
}  // namespace pittore::ai
