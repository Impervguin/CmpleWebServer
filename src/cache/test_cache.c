#include <check.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include "cache/cache.h"

START_TEST(test_create_cache_manager)
{
    CacheParams params = {1000, 10, 100};
    CacheManager *manager = CreateCacheManager(&params);
    ck_assert_ptr_nonnull(manager);
    DestroyCacheManager(manager);
}
END_TEST


START_TEST(test_create_buffer)
{
    CacheParams params = {1000, 10, 100};
    CacheManager *manager = CreateCacheManager(&params);
    int result = CreateBuffer(manager, "key1", 50);
    ck_assert_int_eq(result, ERR_OK);
    DestroyCacheManager(manager);
}
END_TEST

START_TEST(test_create_buffer_size_limit)
{
    CacheParams params = {1000, 10, 50};
    CacheManager *manager = CreateCacheManager(&params);
    int result = CreateBuffer(manager, "key1", 100);
    ck_assert_int_eq(result, ERR_BUFFER_SIZE_LIMIT);
    DestroyCacheManager(manager);
}
END_TEST

START_TEST(test_create_buffer_memory_limit)
{
    CacheParams params = {50, 10, 100};
    CacheManager *manager = CreateCacheManager(&params);
    int result = CreateBuffer(manager, "key1", 40);
    ck_assert_int_eq(result, ERR_OK);
    result = CreateBuffer(manager, "key2", 60);
    ck_assert_int_eq(result, ERR_MEMORY_LIMIT_EXCEEDED);
    DestroyCacheManager(manager);
}
END_TEST

START_TEST(test_get_buffer)
{
    CacheParams params = {1000, 10, 100};
    CacheManager *manager = CreateCacheManager(&params);
    CreateBuffer(manager, "key1", 50);
    ReadBuffer *rb = GetBuffer(manager, "key1");
    ck_assert_ptr_nonnull(rb);
    ReleaseBuffer(rb);
    DestroyCacheManager(manager);
}
END_TEST

START_TEST(test_get_buffer_not_found)
{
    CacheParams params = {1000, 10, 100};
    CacheManager *manager = CreateCacheManager(&params);
    ReadBuffer *rb = GetBuffer(manager, "nonexistent");
    ck_assert_ptr_null(rb);
    DestroyCacheManager(manager);
}
END_TEST

START_TEST(test_get_write_buffer)
{
    CacheParams params = {1000, 10, 100};
    CacheManager *manager = CreateCacheManager(&params);
    CreateBuffer(manager, "key1", 50);
    WriteBuffer *wb = GetWriteBuffer(manager, "key1");
    ck_assert_ptr_nonnull(wb);
    ReleaseWriteBuffer(wb);
    DestroyCacheManager(manager);
}
END_TEST

START_TEST(test_buffer_operations)
{
    CacheParams params = {1000, 10, 100};
    CacheManager *manager = CreateCacheManager(&params);
    CreateBuffer(manager, "key1", 50);
    ReadBuffer *rb = GetBuffer(manager, "key1");
    ck_assert_ptr_nonnull(rb);
    // Check data is accessible
    ck_assert_ptr_nonnull(rb->data);
    ck_assert_int_eq(*rb->size, 50);
    ck_assert_int_eq(*rb->used, 0);
    ReleaseBuffer(rb);
    DestroyCacheManager(manager);
}
END_TEST

START_TEST(test_lru_memory_eviction_with_used_buffers)
{
    CacheParams params = {100, 10, 100};
    CacheManager *manager = CreateCacheManager(&params);
    CreateBuffer(manager, "key1", 50);
    ReadBuffer *rb = GetBuffer(manager, "key1"); // ref=1, can't evict
    ck_assert_ptr_nonnull(rb);
    int result = CreateBuffer(manager, "key2", 60); // 50+60>100, tries to free 50, but can't
    ck_assert_int_eq(result, ERR_MEMORY_LIMIT_EXCEEDED);
    ReleaseBuffer(rb);
    DestroyCacheManager(manager);
}
END_TEST

START_TEST(test_lru_count_eviction_with_used_buffers)
{
    CacheParams params = {1000, 2, 100};
    CacheManager *manager = CreateCacheManager(&params);
    CreateBuffer(manager, "key1", 50);
    CreateBuffer(manager, "key2", 50);
    ReadBuffer *rb1 = GetBuffer(manager, "key1");
    ReadBuffer *rb2 = GetBuffer(manager, "key2"); // both ref=1
    ck_assert_ptr_nonnull(rb1);
    ck_assert_ptr_nonnull(rb2);
    int result = CreateBuffer(manager, "key3", 50); // entry_count=2 == max_entries, tries to free 1, but can't
    ck_assert_int_eq(result, ERR_BUFFER_COUNT_EXCEEDED);
    ReleaseBuffer(rb1);
    ReleaseBuffer(rb2);
    DestroyCacheManager(manager);
}
END_TEST

START_TEST(test_lru_count_popped)
{
    CacheParams params = {1000, 2, 100};
    CacheManager *manager = CreateCacheManager(&params);
    CreateBuffer(manager, "key1", 50);
    CreateBuffer(manager, "key2", 50);
    ReadBuffer *rb1 = GetBuffer(manager, "key1");
    ReadBuffer *rb2 = GetBuffer(manager, "key2"); // both ref=1
    ck_assert_ptr_nonnull(rb1);
    ck_assert_ptr_nonnull(rb2);
    ReleaseBuffer(rb2);
    
    int result = CreateBuffer(manager, "key3", 50);
    ck_assert_int_eq(result, ERR_OK);
    ReadBuffer *rb3 = GetBuffer(manager, "key3");
    ck_assert_ptr_nonnull(rb3);
    ReleaseBuffer(rb1);
    ReleaseBuffer(rb3);
    DestroyCacheManager(manager);
}
END_TEST


START_TEST(test_all_unused_not_enough_memory)
{
    int result;
    CacheParams params = {100, 10, 100};
    CacheManager *manager = CreateCacheManager(&params);
    result = CreateBuffer(manager, "key1", 50);
    ck_assert_int_eq(result, ERR_OK);
    ReadBuffer *rb = GetBuffer(manager, "key1"); // ref=1, can't evict
    ck_assert_ptr_nonnull(rb);
    result = CreateBuffer(manager, "key2", 30);
    ck_assert_int_eq(result, ERR_OK);
    result = CreateBuffer(manager, "key3", 15);
    ck_assert_int_eq(result, ERR_OK);

    result = CreateBuffer(manager, "key4", 55); // if free key2 and key3, still can't insert key4, so key2 and key3 should not be freed
    ck_assert_int_eq(result, ERR_MEMORY_LIMIT_EXCEEDED);

    ReadBuffer *rb2 = GetBuffer(manager, "key2");
    ck_assert_ptr_nonnull(rb2);
    ReleaseBuffer(rb2);

    ReadBuffer *rb3 = GetBuffer(manager, "key3");
    ck_assert_ptr_nonnull(rb3);
    ReleaseBuffer(rb3);

    ReleaseBuffer(rb);
    DestroyCacheManager(manager);
}

START_TEST(test_create_buffer_duplicate_key)
{
    CacheParams params = {1000, 10, 100};
    CacheManager *manager = CreateCacheManager(&params);
    int result = CreateBuffer(manager, "key1", 50);
    ck_assert_int_eq(result, ERR_OK);
    result = CreateBuffer(manager, "key1", 30); // duplicate key
    ck_assert_int_eq(result, ERR_OK); // currently allows, but may be bug
    ReadBuffer *rb = GetBuffer(manager, "key1");
    ck_assert_ptr_nonnull(rb);
    ck_assert_int_eq(*rb->size, 50); // gets first one
    ReleaseBuffer(rb);
    DestroyCacheManager(manager);
}

START_TEST(test_write_and_read_buffer)
{
    CacheParams params = {1000, 10, 100};
    CacheManager *manager = CreateCacheManager(&params);
    CreateBuffer(manager, "key1", 50);
    WriteBuffer *wb = GetWriteBuffer(manager, "key1");
    ck_assert_ptr_nonnull(wb);
    strcpy(wb->data, "hello");
    *wb->used = 5;
    ReleaseWriteBuffer(wb);

    ReadBuffer *rb = GetBuffer(manager, "key1");
    ck_assert_ptr_nonnull(rb);
    ck_assert_int_eq(*rb->used, 5);
    ck_assert_str_eq(rb->data, "hello");
    ReleaseBuffer(rb);
    DestroyCacheManager(manager);
}

START_TEST(test_multiple_references)
{
    CacheParams params = {1000, 10, 100};
    CacheManager *manager = CreateCacheManager(&params);
    CreateBuffer(manager, "key1", 50);
    ReadBuffer *rb1 = GetBuffer(manager, "key1");
    ReadBuffer *rb2 = GetBuffer(manager, "key1");
    ck_assert_ptr_nonnull(rb1);
    ck_assert_ptr_nonnull(rb2);
    // Both should point to same data
    ck_assert_ptr_eq(rb1->data, rb2->data);
    ReleaseBuffer(rb1);
    ReleaseBuffer(rb2);
    DestroyCacheManager(manager);
}

START_TEST(test_lru_eviction_after_release)
{
    CacheParams params = {100, 10, 100};
    CacheManager *manager = CreateCacheManager(&params);
    CreateBuffer(manager, "key1", 50);
    CreateBuffer(manager, "key2", 50);
    ReadBuffer *rb1 = GetBuffer(manager, "key1"); // ref=1
    int result = CreateBuffer(manager, "key3", 50); // should evict key2 since key1 is referenced
    ck_assert_int_eq(result, ERR_OK);
    ReadBuffer *rb2 = GetBuffer(manager, "key2");
    ck_assert_ptr_null(rb2); // key2 evicted
    ReleaseBuffer(rb1);
    DestroyCacheManager(manager);
}

START_TEST(test_buffer_locks)
{
    CacheParams params = {1000, 10, 100};
    CacheManager *manager = CreateCacheManager(&params);
    CreateBuffer(manager, "key1", 50);
    ReadBuffer *rb = GetBuffer(manager, "key1");
    WriteBuffer *wb = GetWriteBuffer(manager, "key1");
    ck_assert_ptr_nonnull(rb);
    ck_assert_ptr_nonnull(wb);
    // Test locks don't crash
    LockReadBuffer(rb);
    UnlockReadBuffer(rb);
    LockWriteBuffer(wb);
    UnlockWriteBuffer(wb);
    ReleaseBuffer(rb);
    ReleaseWriteBuffer(wb);
    DestroyCacheManager(manager);
}

START_TEST(test_destroy_with_active_references)
{
    CacheParams params = {1000, 10, 100};
    CacheManager *manager = CreateCacheManager(&params);
    CreateBuffer(manager, "key1", 50);
    ReadBuffer *rb = GetBuffer(manager, "key1");
    ck_assert_ptr_nonnull(rb);
    // Destroy with active reference - should not crash
    DestroyCacheManager(manager);
    // rb is now dangling, but test passed if no crash
}

START_TEST(test_create_buffer_zero_size)
{
    CacheParams params = {1000, 10, 100};
    CacheManager *manager = CreateCacheManager(&params);
    int result = CreateBuffer(manager, "key1", 0);
    ck_assert_int_eq(result, ERR_OK); // assuming allowed
    ReadBuffer *rb = GetBuffer(manager, "key1");
    ck_assert_ptr_nonnull(rb);
    ck_assert_int_eq(*rb->size, 0);
    ReleaseBuffer(rb);
    DestroyCacheManager(manager);
}

// Add more tests as needed

Suite *cache_suite(void)
{
    Suite *s = suite_create("Cache");

    TCase *tc_core = tcase_create("Core");
    tcase_add_test(tc_core, test_create_cache_manager);
    tcase_add_test(tc_core, test_create_buffer);
    tcase_add_test(tc_core, test_create_buffer_size_limit);
    tcase_add_test(tc_core, test_create_buffer_memory_limit);
    tcase_add_test(tc_core, test_get_buffer);
    tcase_add_test(tc_core, test_get_buffer_not_found);
    tcase_add_test(tc_core, test_get_write_buffer);
    tcase_add_test(tc_core, test_buffer_operations);
    tcase_add_test(tc_core, test_lru_memory_eviction_with_used_buffers);
    tcase_add_test(tc_core, test_lru_count_eviction_with_used_buffers);
    tcase_add_test(tc_core, test_all_unused_not_enough_memory);
    tcase_add_test(tc_core, test_lru_count_popped);
    tcase_add_test(tc_core, test_create_buffer_duplicate_key);
    tcase_add_test(tc_core, test_write_and_read_buffer);
    tcase_add_test(tc_core, test_multiple_references);
    tcase_add_test(tc_core, test_lru_eviction_after_release);
    tcase_add_test(tc_core, test_buffer_locks);
    tcase_add_test(tc_core, test_destroy_with_active_references);
    tcase_add_test(tc_core, test_create_buffer_zero_size);

    suite_add_tcase(s, tc_core);

    return s;
}