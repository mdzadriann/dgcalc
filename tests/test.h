#ifndef DGCALC_TEST_H
#define DGCALC_TEST_H

#include <stdio.h>
#include <math.h>
#include <string.h>

extern int test_checks;
extern int test_failures;

void test_fail(const char *file, int line, const char *message);

#define ASSERT_TRUE(expression)                                                   \
    do {                                                                          \
        test_checks++;                                                            \
        if (!(expression)) {                                                      \
            test_fail(__FILE__, __LINE__, "expected true: " #expression);        \
        }                                                                         \
    } while (0)

#define ASSERT_FALSE(expression) ASSERT_TRUE(!(expression))

#define ASSERT_EQ_INT(expected, actual)                                           \
    do {                                                                          \
        long test_expected = (long)(expected);                                    \
        long test_actual = (long)(actual);                                        \
        test_checks++;                                                            \
        if (test_expected != test_actual) {                                       \
            char test_message[160];                                               \
            snprintf(test_message, sizeof(test_message),                          \
                     "expected %ld, got %ld: %s", test_expected, test_actual,    \
                     #actual);                                                    \
            test_fail(__FILE__, __LINE__, test_message);                          \
        }                                                                         \
    } while (0)

#define ASSERT_STR_EQ(expected, actual)                                           \
    do {                                                                          \
        const char *test_expected = (expected);                                   \
        const char *test_actual = (actual);                                       \
        test_checks++;                                                            \
        if (strcmp(test_expected, test_actual) != 0) {                            \
            char test_message[320];                                               \
            snprintf(test_message, sizeof(test_message),                          \
                     "expected \"%s\", got \"%s\": %s", test_expected,      \
                     test_actual, #actual);                                       \
            test_fail(__FILE__, __LINE__, test_message);                          \
        }                                                                         \
    } while (0)

#define ASSERT_FLOAT_EQ(expected, actual)                                         \
    do {                                                                          \
        float test_expected = (float)(expected);                                  \
        float test_actual = (float)(actual);                                      \
        float test_difference = test_expected - test_actual;                      \
        test_checks++;                                                            \
        if (test_difference < 0.0f) {                                             \
            test_difference = -test_difference;                                   \
        }                                                                         \
        if (!isfinite(test_actual) || test_difference > 0.0001f) {                \
            char test_message[160];                                               \
            snprintf(test_message, sizeof(test_message),                          \
                     "expected %.7g, got %.7g: %s", (double)test_expected,       \
                     (double)test_actual, #actual);                               \
            test_fail(__FILE__, __LINE__, test_message);                          \
        }                                                                         \
    } while (0)

#endif
