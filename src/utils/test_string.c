#include <check.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "utils/string.h"

START_TEST(test_CreateDynamicString_zero_capacity)
{
    DynamicString *str = CreateDynamicString(0);
    ck_assert_ptr_null(str);
}
END_TEST

START_TEST(test_CreateDynamicString_normal)
{
    DynamicString *str = CreateDynamicString(10);
    ck_assert_ptr_nonnull(str);
    ck_assert_int_eq(str->capacity, 10);
    ck_assert_int_eq(str->size, 0);
    ck_assert_ptr_nonnull(str->data);
    DestroyDynamicString(str);
}
END_TEST

START_TEST(test_DestroyDynamicString_null)
{
    DestroyDynamicString(NULL); // Should not crash
}
END_TEST

START_TEST(test_ExpandDynamicString)
{
    DynamicString *str = CreateDynamicString(10);
    int result = ExpandDynamicString(str, 5);
    ck_assert_int_eq(result, ERR_OK);
    ck_assert_int_eq(str->capacity, 15);
    DestroyDynamicString(str);
}
END_TEST

START_TEST(test_AppendDynamicString)
{
    DynamicString *str = CreateDynamicString(10);
    int result = AppendDynamicString(str, "hello", 5);
    ck_assert_int_eq(result, ERR_OK);
    ck_assert_int_eq(str->size, 5);
    ck_assert_str_eq(str->data, "hello");
    ck_assert_int_eq(str->data[5], '\0');
    DestroyDynamicString(str);
}
END_TEST

START_TEST(test_AppendDynamicStringChar)
{
    DynamicString *str = CreateDynamicString(10);
    int result = AppendDynamicStringChar(str, "world");
    ck_assert_int_eq(result, ERR_OK);
    ck_assert_int_eq(str->size, 5);
    ck_assert_str_eq(str->data, "world");
    DestroyDynamicString(str);
}
END_TEST

START_TEST(test_AppendDynamicString_expand)
{
    DynamicString *str = CreateDynamicString(5);
    int result = AppendDynamicString(str, "hello world", 11);
    ck_assert_int_eq(result, ERR_OK);
    ck_assert_int_ge(str->capacity, 11);
    ck_assert_int_eq(str->size, 11);
    ck_assert_str_eq(str->data, "hello world");
    DestroyDynamicString(str);
}
END_TEST

START_TEST(test_SetDynamicString)
{
    DynamicString *str = CreateDynamicString(10);
    int result = SetDynamicString(str, "test", 4);
    ck_assert_int_eq(result, ERR_OK);
    ck_assert_int_eq(str->size, 4);
    ck_assert_str_eq(str->data, "test");
    DestroyDynamicString(str);
}
END_TEST

START_TEST(test_SetDynamicStringChar)
{
    DynamicString *str = CreateDynamicString(10);
    int result = SetDynamicStringChar(str, "example");
    ck_assert_int_eq(result, ERR_OK);
    ck_assert_int_eq(str->size, 7);
    ck_assert_str_eq(str->data, "example");
    DestroyDynamicString(str);
}
END_TEST

START_TEST(test_SetDynamicString_expand)
{
    DynamicString *str = CreateDynamicString(5);
    int result = SetDynamicString(str, "longer string", 13);
    ck_assert_int_eq(result, ERR_OK);
    ck_assert_int_ge(str->capacity, 13);
    ck_assert_int_eq(str->size, 13);
    ck_assert_str_eq(str->data, "longer string");
    DestroyDynamicString(str);
}
END_TEST

START_TEST(test_PrefixDynamicString)
{
    DynamicString *str = CreateDynamicString(10);
    SetDynamicStringChar(str, "world");
    int result = PrefixDynamicString(str, "hello ", 6);
    ck_assert_int_eq(result, ERR_OK);
    ck_assert_int_eq(str->size, 11);
    ck_assert_str_eq(str->data, "hello world");
    DestroyDynamicString(str);
}
END_TEST

START_TEST(test_PrefixDynamicStringChar)
{
    DynamicString *str = CreateDynamicString(10);
    SetDynamicStringChar(str, "end");
    int result = PrefixDynamicStringChar(str, "start ");
    ck_assert_int_eq(result, ERR_OK);
    ck_assert_int_eq(str->size, 9);
    ck_assert_str_eq(str->data, "start end");
    DestroyDynamicString(str);
}
END_TEST

START_TEST(test_PrefixDynamicString_zero)
{
    DynamicString *str = CreateDynamicString(10);
    SetDynamicStringChar(str, "test");
    int result = PrefixDynamicString(str, "", 0);
    ck_assert_int_eq(result, ERR_OK);
    ck_assert_int_eq(str->size, 4);
    ck_assert_str_eq(str->data, "test");
    DestroyDynamicString(str);
}
END_TEST

START_TEST(test_PrefixDynamicString_expand)
{
    DynamicString *str = CreateDynamicString(5);
    SetDynamicStringChar(str, "world");
    int result = PrefixDynamicString(str, "hello ", 6);
    ck_assert_int_eq(result, ERR_OK);
    ck_assert_int_ge(str->capacity, 11);
    ck_assert_int_eq(str->size, 11);
    ck_assert_str_eq(str->data, "hello world");
    DestroyDynamicString(str);
}
END_TEST

START_TEST(test_multiple_operations)
{
    DynamicString *str = CreateDynamicString(10);
    AppendDynamicStringChar(str, "hello");
    AppendDynamicStringChar(str, " ");
    AppendDynamicStringChar(str, "world");
    ck_assert_str_eq(str->data, "hello world");
    ck_assert_int_eq(str->size, 11);
    DestroyDynamicString(str);
}
END_TEST

Suite *string_suite(void)
{
    Suite *s = suite_create("String");

    TCase *tc_core = tcase_create("Core");
    tcase_add_test(tc_core, test_CreateDynamicString_zero_capacity);
    tcase_add_test(tc_core, test_CreateDynamicString_normal);
    tcase_add_test(tc_core, test_DestroyDynamicString_null);
    tcase_add_test(tc_core, test_ExpandDynamicString);
    tcase_add_test(tc_core, test_AppendDynamicString);
    tcase_add_test(tc_core, test_AppendDynamicStringChar);
    tcase_add_test(tc_core, test_AppendDynamicString_expand);
    tcase_add_test(tc_core, test_SetDynamicString);
    tcase_add_test(tc_core, test_SetDynamicStringChar);
    tcase_add_test(tc_core, test_SetDynamicString_expand);
    tcase_add_test(tc_core, test_PrefixDynamicString);
    tcase_add_test(tc_core, test_PrefixDynamicStringChar);
    tcase_add_test(tc_core, test_PrefixDynamicString_zero);
    tcase_add_test(tc_core, test_PrefixDynamicString_expand);
    tcase_add_test(tc_core, test_multiple_operations);

    suite_add_tcase(s, tc_core);

    return s;
}