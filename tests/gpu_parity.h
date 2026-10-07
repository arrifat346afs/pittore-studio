#pragma once
// gpu_parity.h — CPU-vs-GPU kernel parity entry point (see gpu_parity.cpp).
#include "engine/compute/backend.h"

void run_gpu_parity(pittore::compute::BackendType gpu_type);