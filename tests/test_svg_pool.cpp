// Pool units: parallel matches serial, threshold inline.
#include <string>
#include <vector>

#include "engine/vector/svg/thread_pool.h"
#include "test_util.h"

using namespace pittore::svg;

namespace {

void test_match() {
    Pool p(4);
    const int n = 1000;
    std::vector<int> a((size_t)n, 0), b((size_t)n, 0);
    for (int i = 0; i < n; ++i) {
        a[(size_t)i] = i * 3 + 1;
    }
    p.dispatch(n, [&](int i, int) { b[(size_t)i] = a[(size_t)i] * 2; });
    for (int i = 0; i < n; ++i) {
        CHECK_EQ(b[(size_t)i], a[(size_t)i] * 2);
    }
}

void test_threshold() {
    Pool p(4);
    int calls = 0;
    p.dispatch_threshold(10, false, [&](int, int) { ++calls; });
    CHECK_EQ(calls, 10);
    CHECK(p.size() >= 1);
}

void test_empty() {
    Pool p(2);
    p.dispatch(0, [&](int, int) { CHECK(false); });
}

void test_global() {
    setThreads(2);
    CHECK(getPool()->size() == 2);
    setThreads(0);
    CHECK(getPool()->size() >= 1);
}

}  // namespace

int main() {
    test_match();
    test_threshold();
    test_empty();
    test_global();
    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}
