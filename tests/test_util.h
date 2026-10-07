#pragma once
// Minimal zero-dependency test harness. Kept deliberately tiny so the first
// build needs nothing but a C++20 compiler — swap for doctest/gtest later if
// the suite outgrows it.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace pittore_test {

inline int& failures() {
    static int f = 0;
    return f;
}
inline int& checks() {
    static int c = 0;
    return c;
}

inline std::string fmt(double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.9g", v);
    return buf;
}

inline void report_failure(const char* file, int line, const std::string& msg) {
    std::fprintf(stderr, "FAIL %s:%d  %s\n", file, line, msg.c_str());
    ++failures();
}

}  // namespace pittore_test

#define CHECK(cond)                                                            \
    do {                                                                       \
        ++pittore_test::checks();                                             \
        if (!(cond))                                                           \
            pittore_test::report_failure(__FILE__, __LINE__,                  \
                                          "CHECK(" #cond ")");                 \
    } while (0)

#define CHECK_EQ(a, b)                                                         \
    do {                                                                       \
        ++pittore_test::checks();                                             \
        const auto va = (a);                                                   \
        const auto vb = (b);                                                   \
        if (!(va == vb))                                                       \
            pittore_test::report_failure(                                     \
                __FILE__, __LINE__, std::string("CHECK_EQ(" #a ", " #b "): ") +\
                pittore_test::fmt(static_cast<double>(va)) + " vs " +         \
                pittore_test::fmt(static_cast<double>(vb)));                  \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                  \
    do {                                                                       \
        ++pittore_test::checks();                                             \
        const auto va = (a);                                                   \
        const auto vb = (b);                                                   \
        if (!(std::abs(va - vb) <= (eps)))                                     \
            pittore_test::report_failure(                                     \
                __FILE__, __LINE__, std::string("CHECK_NEAR(" #a ", " #b "): ") + \
                pittore_test::fmt(static_cast<double>(va)) + " vs " +         \
                pittore_test::fmt(static_cast<double>(vb)) + " (eps " +       \
                pittore_test::fmt(static_cast<double>(eps)) + ")");           \
    } while (0)

#define TEST_MAIN \
    int main() { \
        const int rc = pittore_test::failures() == 0 ? 0 : 1; \
        std::printf("  checks=%d failures=%d\n", pittore_test::checks(), \
                    pittore_test::failures()); \
        return rc; \
    }

#define TEST_MAIN_CALL(fn) \
    int main() { \
        fn(); \
        const int rc = pittore_test::failures() == 0 ? 0 : 1; \
        std::printf("  checks=%d failures=%d\n", pittore_test::checks(), \
                    pittore_test::failures()); \
        return rc; \
    }
