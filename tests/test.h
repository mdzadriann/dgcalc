#ifndef DGCALC_TEST_H
#define DGCALC_TEST_H

#include <stdio.h>

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

#endif
