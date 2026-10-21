#include "test.h"

int test_checks = 0;
int test_failures = 0;

void run_app_state_tests(void);
void run_editor_tests(void);
void run_parser_tests(void);
void run_eval_tests(void);
void run_project_tests(void);
void run_project_codec_tests(void);
void run_atomic_store_tests(void);
void run_ce_ui_tests(void);
void run_viewport_tests(void);
void run_ce_input_tests(void);
void run_plot2d_tests(void);
void run_project_plot2d_tests(void);
void run_main_loop_tests(void);

void test_fail(const char *file, int line, const char *message) {
    test_failures++;
    fprintf(stderr, "%s:%d: %s\n", file, line, message);
}

int main(void) {
    run_app_state_tests();
    run_editor_tests();
    run_parser_tests();
    run_eval_tests();
    run_project_tests();
    run_project_codec_tests();
    run_atomic_store_tests();
    run_ce_ui_tests();
    run_viewport_tests();
    run_ce_input_tests();
    run_plot2d_tests();
    run_project_plot2d_tests();
    run_main_loop_tests();
    printf("%d checks, %d failures\n", test_checks, test_failures);
    return test_failures == 0 ? 0 : 1;
}
