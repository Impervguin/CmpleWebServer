#include <check.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "utils/date.h"

START_TEST(test_GetHttpDate_valid_time)
{
    time_t test_time = 1609459200; // 2021-01-01 00:00:00 UTC
    DynamicString *result = GetHttpDate(test_time);
    ck_assert_ptr_nonnull(result);
    ck_assert_str_eq(result->data, "Fri, 01 Jan 2021 00:00:00 GMT");
    ck_assert_int_eq(result->size, 29);
    DestroyDynamicString(result);
}
END_TEST

START_TEST(test_GetHttpDate_current_time)
{
    time_t now = time(NULL);
    DynamicString *result = GetHttpDate(now);
    ck_assert_ptr_nonnull(result);
    // Check that it's null-terminated and has expected length
    ck_assert_int_eq(strlen(result->data), 29);
    ck_assert_int_eq(result->size, 29);
    // Check format roughly
    ck_assert_ptr_nonnull(strstr(result->data, " GMT"));
    DestroyDynamicString(result);
}
END_TEST

START_TEST(test_GetHttpDate_epoch)
{
    time_t test_time = 0; // 1970-01-01 00:00:00 UTC
    DynamicString *result = GetHttpDate(test_time);
    ck_assert_ptr_nonnull(result);
    ck_assert_str_eq(result->data, "Thu, 01 Jan 1970 00:00:00 GMT");
    ck_assert_int_eq(result->size, 29);
    DestroyDynamicString(result);
}
END_TEST

START_TEST(test_GetHttpDate_leap_year)
{
    // 2020-02-29 10:40:00 UTC (leap year) timestamp: 1582972800
    time_t test_time = 1582972800;
    DynamicString *result = GetHttpDate(test_time);
    ck_assert_ptr_nonnull(result);
    ck_assert_str_eq(result->data, "Sat, 29 Feb 2020 10:40:00 GMT");
    ck_assert_int_eq(result->size, 29);
    DestroyDynamicString(result);
}
END_TEST

// Note: Testing invalid time_t that causes gmtime to fail is hard, as time_t is typically valid.
// But according to code, if gmtime fails, it returns NULL.

Suite *date_suite(void)
{
    Suite *s = suite_create("Date");

    TCase *tc_core = tcase_create("Core");
    tcase_add_test(tc_core, test_GetHttpDate_valid_time);
    tcase_add_test(tc_core, test_GetHttpDate_current_time);
    tcase_add_test(tc_core, test_GetHttpDate_epoch);
    tcase_add_test(tc_core, test_GetHttpDate_leap_year);

    suite_add_tcase(s, tc_core);

    return s;
}