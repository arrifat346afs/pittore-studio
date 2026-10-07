#pragma once
// gpu_invariants.h — analytic-invariant GPU kernel checks (see gpu_invariants.cpp).
#include "engine/compute/backend.h"

void run_gpu_invariants(pittore::compute::BackendType gpu_type);