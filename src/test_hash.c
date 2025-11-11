#include <check.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "hash.h"

START_TEST(test_hash_empty_string)
{
    size_t table_size = 100;
    unsigned long result = hash("", table_size);
    ck_assert_uint_ge(result, 0);
    ck_assert_uint_lt(result, table_size);
}
END_TEST

START_TEST(test_hash_simple_string)
{
    size_t table_size = 100;
    unsigned long result = hash("hello", table_size);
    ck_assert_uint_ge(result, 0);
    ck_assert_uint_lt(result, table_size);
}
END_TEST

START_TEST(test_hash_consistency)
{
    size_t table_size = 100;
    unsigned long result1 = hash("test", table_size);
    unsigned long result2 = hash("test", table_size);
    ck_assert_uint_eq(result1, result2);
}
END_TEST

START_TEST(test_hash_different_keys)
{
    size_t table_size = 100;
    unsigned long result1 = hash("key1", table_size);
    unsigned long result2 = hash("key2", table_size);
    ck_assert_uint_ge(result1, 0);
    ck_assert_uint_lt(result1, table_size);
    ck_assert_uint_ge(result2, 0);
    ck_assert_uint_lt(result2, table_size);
    ck_assert_uint_ne(result1, result2); // may be  collision
}
END_TEST

START_TEST(test_hash_table_size_one)
{
    size_t table_size = 1;
    unsigned long result = hash("any", table_size);
    ck_assert_uint_eq(result, 0);
}
END_TEST

START_TEST(test_hash_large_table_size)
{
    size_t table_size = 1000000;
    unsigned long result = hash("world", table_size);
    ck_assert_uint_ge(result, 0);
    ck_assert_uint_lt(result, table_size);
}
END_TEST

Suite *hash_suite(void)
{
    Suite *s = suite_create("Hash");

    TCase *tc_core = tcase_create("Core");
    tcase_add_test(tc_core, test_hash_empty_string);
    tcase_add_test(tc_core, test_hash_simple_string);
    tcase_add_test(tc_core, test_hash_consistency);
    tcase_add_test(tc_core, test_hash_different_keys);
    tcase_add_test(tc_core, test_hash_table_size_one);
    tcase_add_test(tc_core, test_hash_large_table_size);

    suite_add_tcase(s, tc_core);

    return s;
}