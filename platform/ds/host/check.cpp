#include "check.h"

namespace ktest {

Case cases[MAX_CASES];
int caseCount = 0;
int failures = 0;
int checks = 0;

int add(const char* name, void (*fn)()) {
    if (caseCount >= MAX_CASES) {
        std::printf("check: more than %d test cases; raise MAX_CASES\n", MAX_CASES);
        ++failures;
        return 0;
    }
    cases[caseCount].name = name;
    cases[caseCount].fn = fn;
    ++caseCount;
    return 0;
}

void fail(const char* file, int line, const char* expr) {
    std::printf("  FAIL %s:%d  %s\n", file, line, expr);
    ++failures;
}

void failEq(const char* file, int line, const char* expr,
            long long got, long long want) {
    std::printf("  FAIL %s:%d  %s\n         got %lld, want %lld\n",
                file, line, expr, got, want);
    ++failures;
}

}  // namespace ktest

int main() {
    for (int i = 0; i < ktest::caseCount; ++i) {
        const int before = ktest::failures;
        ktest::cases[i].fn();
        if (ktest::failures != before)
            std::printf("  in %s\n", ktest::cases[i].name);
    }
    std::printf("%d cases, %d checks, %d failures\n",
                ktest::caseCount, ktest::checks, ktest::failures);
    return ktest::failures == 0 ? 0 : 1;
}
