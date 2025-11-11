#include <check.h>
#include <stdlib.h>

// Declare suite functions from test files
Suite *cache_suite(void);
Suite *hash_suite(void);

int main(void)
{
    int number_failed = 0;

    // Run cache tests
    Suite *s_cache = cache_suite();
    SRunner *sr_cache = srunner_create(s_cache);
    srunner_run_all(sr_cache, CK_NORMAL);
    number_failed += srunner_ntests_failed(sr_cache);
    srunner_free(sr_cache);

    // Run hash tests
    Suite *s_hash = hash_suite();
    SRunner *sr_hash = srunner_create(s_hash);
    srunner_run_all(sr_hash, CK_NORMAL);
    number_failed += srunner_ntests_failed(sr_hash);
    srunner_free(sr_hash);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}