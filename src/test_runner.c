#include <check.h>
#include <stdlib.h>

// Declare suite functions from test files
Suite *cache_suite(void);
Suite *hash_suite(void);
Suite *reader_suite(void);
Suite *content_suite(void);
Suite *date_suite(void);
Suite *log_suite(void);
Suite *string_suite(void);
Suite *strutils_suite(void);

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

    // Run reader tests
    Suite *s_reader = reader_suite();
    SRunner *sr_reader = srunner_create(s_reader);
    srunner_run_all(sr_reader, CK_NORMAL);
    number_failed += srunner_ntests_failed(sr_reader);
    srunner_free(sr_reader);

    // Run content tests
    Suite *s_content = content_suite();
    SRunner *sr_content = srunner_create(s_content);
    srunner_run_all(sr_content, CK_NORMAL);
    number_failed += srunner_ntests_failed(sr_content);
    srunner_free(sr_content);

    // Run date tests
    Suite *s_date = date_suite();
    SRunner *sr_date = srunner_create(s_date);
    srunner_run_all(sr_date, CK_NORMAL);
    number_failed += srunner_ntests_failed(sr_date);
    srunner_free(sr_date);

    // Run log tests
    Suite *s_log = log_suite();
    SRunner *sr_log = srunner_create(s_log);
    srunner_run_all(sr_log, CK_NORMAL);
    number_failed += srunner_ntests_failed(sr_log);
    srunner_free(sr_log);

    // Run string tests
    Suite *s_string = string_suite();
    SRunner *sr_string = srunner_create(s_string);
    srunner_run_all(sr_string, CK_NORMAL);
    number_failed += srunner_ntests_failed(sr_string);
    srunner_free(sr_string);

    // Run strutils tests
    Suite *s_strutils = strutils_suite();
    SRunner *sr_strutils = srunner_create(s_strutils);
    srunner_run_all(sr_strutils, CK_NORMAL);
    number_failed += srunner_ntests_failed(sr_strutils);
    srunner_free(sr_strutils);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}