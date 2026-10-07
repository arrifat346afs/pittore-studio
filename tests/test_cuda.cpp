// CUDA wrapper — exercises the CUDA backend against the CPU reference and
// the analytic photo-op invariants.
#include "engine/compute/backend.h"
#include "test_util.h"
#include "gpu_parity.h"
#include "gpu_invariants.h"

void test_cuda() {
    run_gpu_parity(pittore::compute::BackendType::CUDA);
    run_gpu_invariants(pittore::compute::BackendType::CUDA);
}

#ifndef PITTORE_TEST_NO_MAIN
TEST_MAIN_CALL(test_cuda)
#endif