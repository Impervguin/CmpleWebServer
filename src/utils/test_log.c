#include <check.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "utils/log.h"

START_TEST(test_LogInit)
{
    LogInit(); // Should not crash
    LogInit(); // Multiple calls should be fine
}
END_TEST

START_TEST(test_log_debug)
{
    log_debug("test_func", "Debug message");
    log_debug("test_func", "Debug with %s", "format");
}
END_TEST

START_TEST(test_log_info)
{
    log_info("test_func", "Info message");
    log_info("test_func", "Info with %d", 42);
}
END_TEST

START_TEST(test_log_warn)
{
    log_warn("test_func", "Warn message");
    log_warn("test_func", "Warn with %f", 3.14);
}
END_TEST

START_TEST(test_log_error)
{
    log_error("test_func", "Error message");
    log_error("test_func", "Error with %p", (void*)0x123);
}
END_TEST

START_TEST(test_LogDebug_macro)
{
    LogDebug("Macro debug");
}
END_TEST

START_TEST(test_LogInfo_macro)
{
    LogInfo("Macro info");
}
END_TEST

START_TEST(test_LogWarn_macro)
{
    LogWarn("Macro warn");
}
END_TEST

START_TEST(test_LogError_macro)
{
    LogError("Macro error");
}
END_TEST

START_TEST(test_LogDebugF_macro)
{
    LogDebugF("Macro debug %s", "formatted");
}
END_TEST

START_TEST(test_LogInfoF_macro)
{
    LogInfoF("Macro info %d", 123);
}
END_TEST

START_TEST(test_LogWarnF_macro)
{
    LogWarnF("Macro warn %c", 'x');
}
END_TEST

START_TEST(test_LogErrorF_macro)
{
    LogErrorF("Macro error %x", 0xFF);
}
END_TEST

Suite *log_suite(void)
{
    Suite *s = suite_create("Log");

    TCase *tc_core = tcase_create("Core");
    tcase_add_test(tc_core, test_LogInit);
    tcase_add_test(tc_core, test_log_debug);
    tcase_add_test(tc_core, test_log_info);
    tcase_add_test(tc_core, test_log_warn);
    tcase_add_test(tc_core, test_log_error);
    tcase_add_test(tc_core, test_LogDebug_macro);
    tcase_add_test(tc_core, test_LogInfo_macro);
    tcase_add_test(tc_core, test_LogWarn_macro);
    tcase_add_test(tc_core, test_LogError_macro);
    tcase_add_test(tc_core, test_LogDebugF_macro);
    tcase_add_test(tc_core, test_LogInfoF_macro);
    tcase_add_test(tc_core, test_LogWarnF_macro);
    tcase_add_test(tc_core, test_LogErrorF_macro);

    suite_add_tcase(s, tc_core);

    return s;
}