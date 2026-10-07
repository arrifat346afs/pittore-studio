#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "engine/ai/bg_remove.h"

#ifdef PITTORE_HAS_ONNX
#include <onnxruntime_cxx_api.h>
#endif

namespace pittore::ai {
namespace ai_detail {

#ifdef PITTORE_HAS_ONNX
struct OrtSession {
    Ort::Session session{nullptr};
    std::string provider;
};

extern std::mutex gMutex;

Ort::Env& ortEnv();
std::unordered_map<std::string, std::unique_ptr<OrtSession>>& sessionCache();
OrtSession* getOrLoadSessionLocked(const std::string& path, const char* label,
                                   std::string& err, double msNow);
#endif

}  // namespace ai_detail
}  // namespace pittore::ai
