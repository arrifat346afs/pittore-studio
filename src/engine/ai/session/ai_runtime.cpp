#include "engine/ai/bg_remove.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "engine/core/log.h"

#ifdef PITTORE_HAS_ONNX
#include <onnxruntime_cxx_api.h>
#endif

#include "engine/ai/shared/ai_session.h"
#include "engine/ai/shared/ai_preprocess.h"

namespace pittore::ai {

using namespace ai_detail;


bool onnx_available() {
#ifdef PITTORE_HAS_ONNX
    return true;
#else
    return false;
#endif
}


std::string onnx_version() {
#ifdef PITTORE_HAS_ONNX
    return Ort::GetVersionString();
#else
    return {};
#endif
}


void clear_session_cache() {
#ifdef PITTORE_HAS_ONNX
    std::lock_guard<std::mutex> lock(gMutex);
    sessionCache().clear();
#endif
}

}  // namespace pittore::ai
