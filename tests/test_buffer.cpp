// Buffer host-staging round trip on the CPU backend.
#include <cstdint>

#include "engine/compute/factory.h"
#include "test_util.h"

void test_buffer() {
    auto backend = pittore::compute::make_backend(pittore::compute::BackendType::CPU);
    CHECK(backend != nullptr);
    CHECK(backend->type() == pittore::compute::BackendType::CPU);

    auto buf = backend->make_buffer(1024);
    CHECK(buf != nullptr);
    CHECK(buf->size() == 1024);

    auto* bytes = static_cast<unsigned char*>(buf->host());
    for (std::size_t i = 0; i < 1024; ++i) bytes[i] = static_cast<unsigned char>(i & 0xFF);
    buf->upload();
    buf->download();
    for (std::size_t i = 0; i < 1024; ++i)
        CHECK(static_cast<unsigned char>(i & 0xFF) == bytes[i]);
}

#ifndef PITTORE_TEST_NO_MAIN
TEST_MAIN_CALL(test_buffer)
#endif