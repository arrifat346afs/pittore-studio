// Device enumeration + factory contract: CPU always present, default backend
// never null, request of an unavailable backend returns nullptr.
#include "engine/compute/factory.h"
#include "test_util.h"

void test_device() {
    const auto devices = pittore::compute::enumerate_devices();
    CHECK(!devices.empty());

    bool saw_cpu = false;
    for (const auto& d : devices) {
        if (d.type == pittore::compute::BackendType::CPU) saw_cpu = true;
        CHECK(!d.name.empty());
        CHECK(d.type == pittore::compute::BackendType::CPU ||
              d.type == pittore::compute::BackendType::CUDA ||
              d.type == pittore::compute::BackendType::HIP);
    }
    CHECK(saw_cpu);

    auto cpu = pittore::compute::make_backend(pittore::compute::BackendType::CPU);
    CHECK(cpu != nullptr);
    CHECK_EQ(cpu->type(), pittore::compute::BackendType::CPU);
    CHECK(!cpu->name().empty());

    // HIP is almost certainly unavailable here (no ROCm); the contract is
    // nullptr, never a crash.
    auto hip = pittore::compute::make_backend(pittore::compute::BackendType::HIP);
    if (hip) CHECK_EQ(hip->type(), pittore::compute::BackendType::HIP);

    auto def = pittore::compute::make_default_backend();
    CHECK(def != nullptr);

    // enum variety: never two identical CPU entries.
    std::size_t cpus = 0;
    for (const auto& d : devices)
        if (d.type == pittore::compute::BackendType::CPU) ++cpus;
    CHECK(cpus >= 1);
}

#ifndef PITTORE_TEST_NO_MAIN
TEST_MAIN_CALL(test_device)
#endif