// HIP wrapper — exercises the HIP backend against the CPU reference and the
// analytic photo-op invariants. Only built when the HIP backend is compiled in
// (-Dbackend-hip=enabled); at run time it skips cleanly if no AMD device is
// present.
#include "engine/compute/backend.h"
#include "test_util.h"
#include "gpu_parity.h"
#include "gpu_invariants.h"

void test_hip() {
    run_gpu_parity(pittore::compute::BackendType::HIP);
    run_gpu_invariants(pittore::compute::BackendType::HIP);
}

#ifndef PITTORE_TEST_NO_MAIN
TEST_MAIN_CALL(test_hip)
#endif