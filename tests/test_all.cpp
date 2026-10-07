// test_all.cpp — single-binary test runner for Pittore Studio compute backends.
//
// Build on the target machine:
//   meson setup build -Dbackend-cuda=auto -Dbackend-hip=auto
//   ninja -C build tests/test-all
//   ./build/tests/test-all
//
// The binary tests whichever backends were compiled in.  On an NVIDIA box
// it exercises CUDA parity; on an AMD box it exercises HIP parity; on a
// CPU-only box it still runs every host-side kernel test.

#define PITTORE_TEST_NO_MAIN 1

#include "test_buffer.cpp"
#include "test_grayscale.cpp"
#include "test_blend.cpp"
#include "test_blur.cpp"
#include "test_brush.cpp"
#include "test_device.cpp"
#include "test_tile.cpp"
#include "test_cuda.cpp"
#include "test_hip.cpp"
#include "bench_gpu.cpp"

#include <cstdio>
#include <cstring>

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--bench") == 0) {
            run_benchmarks();
            return 0;
        }

    std::printf("=== Pittore Studio — compute engine test suite ===\n\n");

    std::printf("--- test_buffer ---\n");
    test_buffer();
    std::printf("--- test_grayscale ---\n");
    test_grayscale();
    std::printf("--- test_blend ---\n");
    test_blend();
    std::printf("--- test_blur ---\n");
    test_blur();
    std::printf("--- test_brush ---\n");
    test_brush();
    std::printf("--- test_device ---\n");
    test_device();
    std::printf("--- test_tile ---\n");
    test_tile();
    std::printf("--- test_cuda (GPU kernel parity + invariants) ---\n");
    test_cuda();
    std::printf("--- test_hip (GPU kernel parity + invariants) ---\n");
    test_hip();

    std::printf("\n=== all done ===\n");
    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}
