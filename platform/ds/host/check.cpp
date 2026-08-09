#include "check.h"

namespace ktest {

Case cases[MAX_CASES];
int caseCount = 0;
int failures = 0;
int checks = 0;

// A name is what a failure is reported as, so two cases sharing one is two
// failures nobody can tell apart.  KH_TEST makes the function static, so the
// linker will not catch it: only this will.
static bool sameName(const char* a, const char* b) {
    for (; *a && *a == *b; ++a, ++b) {}
    return *a == *b;
}

int add(const char* name, void (*fn)()) {
    if (caseCount >= MAX_CASES) {
        std::printf("check: more than %d test cases; raise MAX_CASES\n", MAX_CASES);
        ++failures;
        return 0;
    }
    for (int i = 0; i < caseCount; ++i) {
        if (sameName(cases[i].name, name)) {
            std::printf("check: two cases are both called %s; a failure in "
                        "either would be reported as the other\n", name);
            ++failures;
            break;
        }
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

// --reverse runs the same cases in the opposite order.
//
// Every case shares one process with every other, and a good deal of file-scope
// state with the ones in its own file: buffers, a Camera, a loaded collision
// map.  A case that only passes because an earlier one left something behind is
// a real failure and an invisible one -- it goes red the day somebody inserts a
// test above it, and the blame lands on the insertion.  Running the set
// backwards is the cheapest thing that finds that, and Makefile.host's `run`
// does both orders every time.
int main(int argc, char** argv) {
    bool reverse = false;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (a[0] == '-' && a[1] == '-' && a[2] == 'r')
            reverse = true;
    }
    for (int n = 0; n < ktest::caseCount; ++n) {
        const int i = reverse ? ktest::caseCount - 1 - n : n;
        const int before = ktest::failures;
        ktest::cases[i].fn();
        if (ktest::failures != before)
            std::printf("  in %s\n", ktest::cases[i].name);
    }
    std::printf("%d cases, %d checks, %d failures%s\n",
                ktest::caseCount, ktest::checks, ktest::failures,
                reverse ? " (reversed)" : "");
    return ktest::failures == 0 ? 0 : 1;
}
