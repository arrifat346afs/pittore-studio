#pragma once
#include <memory>
#include <vector>

#include "engine/compute/backend.h"

namespace pittore::compute {

// Devices currently usable on this machine: CPU always, CUDA/HIP when their
// runtimes report devices at call time.
std::vector<Device> enumerate_devices();

// Create a backend for an explicit type; nullptr when unavailable (e.g. HIP on
// a machine with no ROCm install, CUDA when the toolkit wasn't built in).
std::unique_ptr<ComputeBackend> make_backend(BackendType type);

// Best available: first usable GPU (CUDA, then HIP), else the CPU reference.
std::unique_ptr<ComputeBackend> make_default_backend();

}  // namespace pittore::compute