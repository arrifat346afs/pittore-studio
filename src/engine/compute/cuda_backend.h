#pragma once
// Declarations for the CUDA compute backend. Compiles only when
// PITTORE_HAS_CUDA is defined (meson option -Dbackend-cuda=enabled). This
// header is intentionally free of CUDA runtime includes so the rest of the
// engine can link it without dragging in the toolkit.

#if defined(PITTORE_HAS_CUDA)
#include <memory>
#include <vector>

#include "engine/compute/backend.h"

namespace pittore::compute {

// Devices reported by the CUDA runtime on this machine (empty when none).
std::vector<Device> cuda_devices();

// Backend bound to the first usable CUDA device, or nullptr when none.
std::unique_ptr<ComputeBackend> make_cuda_backend();

}  // namespace pittore::compute
#endif  // PITTORE_HAS_CUDA