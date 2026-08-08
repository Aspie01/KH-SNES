// check.h -- the host test harness, such as it is.
//
// No test framework, on purpose: the device tier cannot link one, and a
// dependency that only exists on the host is a dependency that rots. This is a
// registry, an assertion macro and a return code.
//
// Tests self-register, so adding a test file never requires editing a shared
// file. That is a coordination rule, not a convenience -- see
// docs/DS_PORT_PROMPT.md section 0.7. Drop tests/test_<yourtask>.cpp in and the
// glob in Makefile.host picks it up.

#pragma once

#include <cstdio>
#include <cstdint>

namespace ktest {

constexpr int MAX_CASES = 256;

struct Case {
    const char* name;
    void (*fn)();
};

// These are zero-initialised before any dynamic initialisation runs, which is
// what makes self-registration safe with no heap and no init-order dance.
extern Case cases[MAX_CASES];
extern int caseCount;
extern int failures;
extern int checks;

int add(const char* name, void (*fn)());
void fail(const char* file, int line, const char* expr);
void failEq(const char* file, int line, const char* expr,
            long long got, long long want);

}  // namespace ktest

#define KH_TEST(name)                                                        \
    static void name();                                                      \
    [[maybe_unused]] static const int kh_reg_##name = ktest::add(#name, name); \
    static void name()

#define CHECK(expr)                                                          \
    do {                                                                     \
        ++ktest::checks;                                                     \
        if (!(expr)) ktest::fail(__FILE__, __LINE__, #expr);                 \
    } while (0)

// Prints both sides, because "CHECK(a == b) failed" tells you nothing about a
// fixed-point value you got wrong by a factor of sixteen.
#define CHECK_EQ(got, want)                                                  \
    do {                                                                     \
        ++ktest::checks;                                                     \
        const long long kh_g = static_cast<long long>(got);                  \
        const long long kh_w = static_cast<long long>(want);                 \
        if (kh_g != kh_w)                                                    \
            ktest::failEq(__FILE__, __LINE__, #got " == " #want, kh_g, kh_w); \
    } while (0)
