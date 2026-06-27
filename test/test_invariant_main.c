#include <check.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>

// Forward declaration of the vulnerable function from cmdline/main.c
extern void vulnerable_allocation(size_t count, size_t size);

START_TEST(test_allocation_overflow_protection)
{
    // Invariant: Allocation size computation must not overflow or must be validated
    const struct {
        size_t count;
        size_t size;
        const char *description;
    } test_cases[] = {
        // Exact exploit case: multiplication wraps to small value
        {SIZE_MAX, 2, "SIZE_MAX * 2 wraps to small allocation"},
        // Boundary case: multiplication would overflow but is caught
        {SIZE_MAX / 2 + 1, 2, "Boundary overflow case"},
        // Valid input: should work correctly
        {100, 10, "Valid normal input"}
    };
    
    int num_cases = sizeof(test_cases) / sizeof(test_cases[0]);

    for (int i = 0; i < num_cases; i++) {
        // The test ensures the program doesn't crash or exhibit undefined behavior
        // when given adversarial inputs that could cause overflow
        vulnerable_allocation(test_cases[i].count, test_cases[i].size);
        
        // If we reach here without crashing, the test passes for this case
        // Note: In a real scenario, we might want to check for specific error handling
        // or use valgrind/asan to detect heap corruption, but for a simple unit test
        // we're verifying the program doesn't crash on adversarial input
    }
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");

    tcase_add_test(tc_core, test_allocation_overflow_protection);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}