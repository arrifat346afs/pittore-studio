#pragma once
// Declarations for the HIP compute backend. Compiles only when
// PITTORE_HAS_HIP is defined (meson option -Dbackend-hip=enabled). This
// header is free of HIP runtime includes so the rest of the engine stays
// buildable without ROCm installed.

#if defined(PITTORE_HAS_HIP)
#include <memory>
#include <vector>

#include "engine/compute/backend.h"

namespace pittore::compute {

// Devices reported by the HIP runtime on this machine (empty when none).
std::vector<Device> hip_devices();

// Backend bound to the first usable HIP device, or nullptr when none.
std::unique_ptr<ComputeBackend> make_hip_backend();

}  // namespace pittore::compute
#endif  // PITTORE_HAS_HIP