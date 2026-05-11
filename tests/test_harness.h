/* ============================================================================
 * test_harness.h — Zero-dependency minimal test framework for C99
 * 
 * Usage:
 *   void test_something(void) {
 *       ASSERT_TRUE(1 == 1);
 *       ASSERT_NEAR(3.14, 3.14159, 0.01);
 *   }
 *   
 *   int main(void) {
 *       RUN_TEST(test_something);
 *       TEST_REPORT();
 *       return test_failures;
 *   }
 * ============================================================================ */

#ifndef TEST_HARNESS_H
#define TEST_HARNESS_H

#include <stdio.h>
#include <math.h>

/* --- Global counters ---
 * Declared `extern` here and defined exactly once in test_main.c. The previous
 * `static int ...` pattern gave every TU its own private copy, so an
 * ASSERT_FAIL in test_tensor.c would increment that TU's failure counter while
 * RUN_TEST in test_main.c read its own (always-zero) counter — making failing
 * tests report as passed. The shared `extern` definition also silences gcc's
 * -Werror=unused-variable in the per-test TUs that don't call RUN_TEST. */
extern int test_count;
extern int test_failures;
extern int test_passes;

/* --- ANSI color codes for terminal output --- */
#define CLR_RED    "\033[1;31m"
#define CLR_GREEN  "\033[1;32m"
#define CLR_YELLOW "\033[1;33m"
#define CLR_CYAN   "\033[1;36m"
#define CLR_RESET  "\033[0m"

/* --- Core assertion macros --- */

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        printf(CLR_RED "    FAIL" CLR_RESET " %s:%d: ASSERT_TRUE(%s)\n", \
               __FILE__, __LINE__, #expr); \
        test_failures++; \
        return; \
    } \
} while (0)

#define ASSERT_FALSE(expr) do { \
    if ((expr)) { \
        printf(CLR_RED "    FAIL" CLR_RESET " %s:%d: ASSERT_FALSE(%s)\n", \
               __FILE__, __LINE__, #expr); \
        test_failures++; \
        return; \
    } \
} while (0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf(CLR_RED "    FAIL" CLR_RESET " %s:%d: ASSERT_EQ(%s, %s) — got %d != %d\n", \
               __FILE__, __LINE__, #a, #b, (int)(a), (int)(b)); \
        test_failures++; \
        return; \
    } \
} while (0)

#define ASSERT_EQ_DBL(a, b) do { \
    if ((a) != (b)) { \
        printf(CLR_RED "    FAIL" CLR_RESET " %s:%d: ASSERT_EQ_DBL(%s, %s) — got %.6f != %.6f\n", \
               __FILE__, __LINE__, #a, #b, (double)(a), (double)(b)); \
        test_failures++; \
        return; \
    } \
} while (0)

#define ASSERT_NEQ(a, b) do { \
    if ((a) == (b)) { \
        printf(CLR_RED "    FAIL" CLR_RESET " %s:%d: ASSERT_NEQ(%s, %s) — both are %d\n", \
               __FILE__, __LINE__, #a, #b, (int)(a)); \
        test_failures++; \
        return; \
    } \
} while (0)

#define ASSERT_NEAR(a, b, eps) do { \
    double _a = (double)(a); \
    double _b = (double)(b); \
    double _eps = (double)(eps); \
    if (fabs(_a - _b) > _eps) { \
        printf(CLR_RED "    FAIL" CLR_RESET " %s:%d: ASSERT_NEAR(%s, %s, %s) — |%.8f - %.8f| = %.8f > %.8f\n", \
               __FILE__, __LINE__, #a, #b, #eps, _a, _b, fabs(_a - _b), _eps); \
        test_failures++; \
        return; \
    } \
} while (0)

#define ASSERT_NULL(ptr) do { \
    if ((ptr) != NULL) { \
        printf(CLR_RED "    FAIL" CLR_RESET " %s:%d: ASSERT_NULL(%s) — pointer is not NULL\n", \
               __FILE__, __LINE__, #ptr); \
        test_failures++; \
        return; \
    } \
} while (0)

#define ASSERT_NOT_NULL(ptr) do { \
    if ((ptr) == NULL) { \
        printf(CLR_RED "    FAIL" CLR_RESET " %s:%d: ASSERT_NOT_NULL(%s) — pointer is NULL\n", \
               __FILE__, __LINE__, #ptr); \
        test_failures++; \
        return; \
    } \
} while (0)

/* --- Test runner macro --- */

#define RUN_TEST(fn) do { \
    int _prev_failures = test_failures; \
    test_count++; \
    printf(CLR_CYAN "  [TEST]" CLR_RESET " %s ... ", #fn); \
    fn(); \
    if (test_failures == _prev_failures) { \
        test_passes++; \
        printf(CLR_GREEN "PASS" CLR_RESET "\n"); \
    } else { \
        printf("\n"); \
    } \
} while (0)

/* --- Suite header --- */

#define RUN_SUITE(name) do { \
    printf("\n" CLR_YELLOW "━━━ %s ━━━" CLR_RESET "\n", name); \
} while (0)

/* --- Final report --- */

#define TEST_REPORT() do { \
    printf("\n" CLR_YELLOW "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" CLR_RESET "\n"); \
    printf("  Total: %d  |  ", test_count); \
    printf(CLR_GREEN "Passed: %d" CLR_RESET "  |  ", test_passes); \
    if (test_failures > 0) { \
        printf(CLR_RED "Failed: %d" CLR_RESET, test_failures); \
    } else { \
        printf(CLR_GREEN "Failed: 0" CLR_RESET); \
    } \
    printf("\n" CLR_YELLOW "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" CLR_RESET "\n\n"); \
    if (test_failures == 0) { \
        printf(CLR_GREEN "  ✓ All tests passed!" CLR_RESET "\n\n"); \
    } else { \
        printf(CLR_RED "  ✗ Some tests failed." CLR_RESET "\n\n"); \
    } \
} while (0)

#endif /* TEST_HARNESS_H */
