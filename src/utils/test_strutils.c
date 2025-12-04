#include <check.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "utils/strutils.h"

START_TEST(test_strnstr_found)
{
    const char *s = "hello world";
    char *result = strnstr(s, "world", strlen(s));
    ck_assert_ptr_nonnull(result);
    ck_assert_ptr_eq(result, s + 6);
    ck_assert_str_eq(result, "world");
}
END_TEST

START_TEST(test_strnstr_not_found)
{
    const char *s = "hello world";
    char *result = strnstr(s, "notfound", strlen(s));
    ck_assert_ptr_null(result);
}
END_TEST

START_TEST(test_strnstr_empty_find)
{
    const char *s = "hello";
    char *result = strnstr(s, "", strlen(s));
    ck_assert_ptr_eq(result, s);
}
END_TEST

START_TEST(test_strnstr_empty_s)
{
    const char *s = "";
    char *result = strnstr(s, "find", 0);
    ck_assert_ptr_null(result);
}
END_TEST

START_TEST(test_strnstr_at_start)
{
    const char *s = "hello world";
    char *result = strnstr(s, "hello", strlen(s));
    ck_assert_ptr_eq(result, s);
}
END_TEST

START_TEST(test_strnstr_at_end)
{
    const char *s = "hello world";
    char *result = strnstr(s, "world", strlen(s));
    ck_assert_ptr_eq(result, s + 6);
}
END_TEST

START_TEST(test_strnstr_multiple)
{
    const char *s = "test test test";
    char *result = strnstr(s, "test", strlen(s));
    ck_assert_ptr_eq(result, s);
}
END_TEST

START_TEST(test_strnstr_partial_match_due_to_len)
{
    const char *s = "hello world extra";
    char *result = strnstr(s, "world", 10); // "hello wor" length 9, so "world" not fully in first 10
    ck_assert_ptr_null(result);
}
END_TEST

START_TEST(test_strnstr_len_zero)
{
    const char *s = "hello";
    char *result = strnstr(s, "h", 0);
    ck_assert_ptr_null(result);
}
END_TEST

START_TEST(test_strnstr_len_smaller_than_find)
{
    const char *s = "hi";
    char *result = strnstr(s, "hello", strlen(s));
    ck_assert_ptr_null(result);
}
END_TEST

START_TEST(test_strnstr_overlapping)
{
    const char *s = "aaa";
    char *result = strnstr(s, "aa", strlen(s));
    ck_assert_ptr_eq(result, s);
}
END_TEST

Suite *strutils_suite(void)
{
    Suite *s = suite_create("StrUtils");

    TCase *tc_core = tcase_create("Core");
    tcase_add_test(tc_core, test_strnstr_found);
    tcase_add_test(tc_core, test_strnstr_not_found);
    tcase_add_test(tc_core, test_strnstr_empty_find);
    tcase_add_test(tc_core, test_strnstr_empty_s);
    tcase_add_test(tc_core, test_strnstr_at_start);
    tcase_add_test(tc_core, test_strnstr_at_end);
    tcase_add_test(tc_core, test_strnstr_multiple);
    tcase_add_test(tc_core, test_strnstr_partial_match_due_to_len);
    tcase_add_test(tc_core, test_strnstr_len_zero);
    tcase_add_test(tc_core, test_strnstr_len_smaller_than_find);
    tcase_add_test(tc_core, test_strnstr_overlapping);

    suite_add_tcase(s, tc_core);

    return s;
}