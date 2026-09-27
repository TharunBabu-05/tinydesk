/*
 * td_test.h - a minimal test helper: CHECK() records failures and the
 * test's main() returns TD_TEST_RESULT().
 */
#ifndef TD_TEST_H
#define TD_TEST_H

#include <stdio.h>
#include <string.h>

static int td_test_failures;
static int td_test_checks;

#define CHECK(cond)                                                          \
    do {                                                                     \
        td_test_checks++;                                                    \
        if (!(cond)) {                                                       \
            td_test_failures++;                                              \
            printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);  \
        }                                                                    \
    } while (0)

#define CHECK_EQ(a, b)                                                       \
    do {                                                                     \
        long long va_ = (long long)(a), vb_ = (long long)(b);                \
        td_test_checks++;                                                    \
        if (va_ != vb_) {                                                    \
            td_test_failures++;                                              \
            printf("%s:%d: %s == %s failed (%lld vs %lld)\n", __FILE__,      \
                   __LINE__, #a, #b, va_, vb_);                              \
        }                                                                    \
    } while (0)

#define TD_TEST_RESULT()                                                     \
    (printf("%d checks, %d failures\n", td_test_checks, td_test_failures),   \
     td_test_failures ? 1 : 0)

#endif
