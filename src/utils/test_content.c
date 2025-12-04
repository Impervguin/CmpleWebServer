#include <check.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "utils/content.h"

START_TEST(test_GetContentType_null_path)
{
    ContentType result = GetContentType(NULL);
    ck_assert_int_eq(result, CONTENT_TYPE_TEXT_PLAIN);
}
END_TEST

START_TEST(test_GetContentType_no_extension)
{
    ContentType result = GetContentType("file");
    ck_assert_int_eq(result, CONTENT_TYPE_TEXT_PLAIN);
}
END_TEST

START_TEST(test_GetContentType_unknown_extension)
{
    ContentType result = GetContentType("file.unknown");
    ck_assert_int_eq(result, CONTENT_TYPE_APPLICATION_OCTET_STREAM);
}
END_TEST

START_TEST(test_GetContentType_html)
{
    ContentType result = GetContentType("index.html");
    ck_assert_int_eq(result, CONTENT_TYPE_TEXT_HTML);
}
END_TEST

START_TEST(test_GetContentType_css)
{
    ContentType result = GetContentType("style.css");
    ck_assert_int_eq(result, CONTENT_TYPE_TEXT_CSS);
}
END_TEST

START_TEST(test_GetContentType_js)
{
    ContentType result = GetContentType("script.js");
    ck_assert_int_eq(result, CONTENT_TYPE_APPLICATION_JAVASCRIPT);
}
END_TEST

START_TEST(test_GetContentType_json)
{
    ContentType result = GetContentType("data.json");
    ck_assert_int_eq(result, CONTENT_TYPE_APPLICATION_JSON);
}
END_TEST

START_TEST(test_GetContentType_png)
{
    ContentType result = GetContentType("image.png");
    ck_assert_int_eq(result, CONTENT_TYPE_IMAGE_PNG);
}
END_TEST

START_TEST(test_GetContentType_jpeg)
{
    ContentType result = GetContentType("photo.jpeg");
    ck_assert_int_eq(result, CONTENT_TYPE_IMAGE_JPEG);
}
END_TEST

START_TEST(test_GetContentType_mp3)
{
    ContentType result = GetContentType("music.mp3");
    ck_assert_int_eq(result, CONTENT_TYPE_AUDIO_MPEG);
}
END_TEST

START_TEST(test_GetContentType_mp4)
{
    ContentType result = GetContentType("video.mp4");
    ck_assert_int_eq(result, CONTENT_TYPE_VIDEO_MP4);
}
END_TEST

START_TEST(test_GetContentType_pdf)
{
    ContentType result = GetContentType("doc.pdf");
    ck_assert_int_eq(result, CONTENT_TYPE_APPLICATION_PDF);
}
END_TEST

START_TEST(test_GetContentType_zip)
{
    ContentType result = GetContentType("archive.zip");
    ck_assert_int_eq(result, CONTENT_TYPE_APPLICATION_ZIP);
}
END_TEST

START_TEST(test_GetContentTypeString_text_plain)
{
    const char *result = GetContentTypeString(CONTENT_TYPE_TEXT_PLAIN);
    ck_assert_str_eq(result, TEXT_PLAIN_CONTENT_TYPE);
}
END_TEST

START_TEST(test_GetContentTypeString_text_html)
{
    const char *result = GetContentTypeString(CONTENT_TYPE_TEXT_HTML);
    ck_assert_str_eq(result, TEXT_HTML_CONTENT_TYPE);
}
END_TEST

START_TEST(test_GetContentTypeString_application_json)
{
    const char *result = GetContentTypeString(CONTENT_TYPE_APPLICATION_JSON);
    ck_assert_str_eq(result, APPLICATION_JSON_CONTENT_TYPE);
}
END_TEST

START_TEST(test_GetContentTypeString_application_octet_stream)
{
    const char *result = GetContentTypeString(CONTENT_TYPE_APPLICATION_OCTET_STREAM);
    ck_assert_str_eq(result, APPLICATION_OCTET_STREAM_CONTENT_TYPE);
}
END_TEST

START_TEST(test_GetContentTypeString_unknown)
{
    const char *result = GetContentTypeString((ContentType)999);
    ck_assert_str_eq(result, APPLICATION_OCTET_STREAM_CONTENT_TYPE);
}
END_TEST

START_TEST(test_ContentTypeByPath_null)
{
    const char *result = ContentTypeByPath(NULL);
    ck_assert_str_eq(result, TEXT_PLAIN_CONTENT_TYPE);
}
END_TEST

START_TEST(test_ContentTypeByPath_html)
{
    const char *result = ContentTypeByPath("page.html");
    ck_assert_str_eq(result, TEXT_HTML_CONTENT_TYPE);
}
END_TEST

START_TEST(test_ContentTypeByPath_unknown)
{
    const char *result = ContentTypeByPath("file.xyz");
    ck_assert_str_eq(result, APPLICATION_OCTET_STREAM_CONTENT_TYPE);
}
END_TEST

Suite *content_suite(void)
{
    Suite *s = suite_create("Content");

    TCase *tc_core = tcase_create("Core");
    tcase_add_test(tc_core, test_GetContentType_null_path);
    tcase_add_test(tc_core, test_GetContentType_no_extension);
    tcase_add_test(tc_core, test_GetContentType_unknown_extension);
    tcase_add_test(tc_core, test_GetContentType_html);
    tcase_add_test(tc_core, test_GetContentType_css);
    tcase_add_test(tc_core, test_GetContentType_js);
    tcase_add_test(tc_core, test_GetContentType_json);
    tcase_add_test(tc_core, test_GetContentType_png);
    tcase_add_test(tc_core, test_GetContentType_jpeg);
    tcase_add_test(tc_core, test_GetContentType_mp3);
    tcase_add_test(tc_core, test_GetContentType_mp4);
    tcase_add_test(tc_core, test_GetContentType_pdf);
    tcase_add_test(tc_core, test_GetContentType_zip);
    tcase_add_test(tc_core, test_GetContentTypeString_text_plain);
    tcase_add_test(tc_core, test_GetContentTypeString_text_html);
    tcase_add_test(tc_core, test_GetContentTypeString_application_json);
    tcase_add_test(tc_core, test_GetContentTypeString_application_octet_stream);
    tcase_add_test(tc_core, test_GetContentTypeString_unknown);
    tcase_add_test(tc_core, test_ContentTypeByPath_null);
    tcase_add_test(tc_core, test_ContentTypeByPath_html);
    tcase_add_test(tc_core, test_ContentTypeByPath_unknown);

    suite_add_tcase(s, tc_core);

    return s;
}