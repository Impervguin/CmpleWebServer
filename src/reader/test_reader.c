#include <sys/stat.h>
#include <check.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <uuid/uuid.h>
#include "reader/reader.h"
#include "reader/stat.h"

// Global variables for callback testing
#define MAX_RESPONSES 20
static FileReadResponse *responses[MAX_RESPONSES];
static int response_count = 0;
static pthread_mutex_t response_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t response_cond = PTHREAD_COND_INITIALIZER;

void test_callback(FileReadResponse *response, void *userData __attribute__((unused))) {
    pthread_mutex_lock(&response_mutex);
    if (response_count < MAX_RESPONSES) {
        responses[response_count] = malloc(sizeof(FileReadResponse));
        memcpy(responses[response_count], response, sizeof(FileReadResponse));
        response_count++;
    }
    pthread_cond_signal(&response_cond);
    pthread_mutex_unlock(&response_mutex);
}

// Helper to reset responses
void reset_responses(void) {
    pthread_mutex_lock(&response_mutex);
    for (int i = 0; i < response_count; i++) {
        free(responses[i]);
        responses[i] = NULL;
    }
    response_count = 0;
    pthread_mutex_unlock(&response_mutex);
}

// Helper to wait for responses
void wait_for_responses(int count) {
    pthread_mutex_lock(&response_mutex);
    while (response_count < count) {
        pthread_cond_wait(&response_cond, &response_mutex);
    }
    pthread_mutex_unlock(&response_mutex);
}

// Stat tests
START_TEST(test_get_file_stat_existing_file)
{
    FileStatResponse resp = GetFileStat("testdata/test.txt");
    ck_assert_int_eq(resp.error, ERR_OK);
    ck_assert_int_eq(resp.type, RegulatFile);
    ck_assert_int_eq(resp.file_size, 12); // "Hello World\n" is 12 bytes
}
END_TEST

START_TEST(test_get_file_stat_nonexistent_file)
{
    FileStatResponse resp = GetFileStat("testdata/nonexistent.txt");
    ck_assert_int_eq(resp.error, ERR_STAT_FILE_NOT_FOUND);
}
END_TEST

START_TEST(test_get_file_stat_directory)
{
    FileStatResponse resp = GetFileStat("testdata");
    ck_assert_int_eq(resp.error, ERR_OK);
    ck_assert_int_eq(resp.type, Directory);
}
END_TEST

START_TEST(test_get_file_stat_null_path)
{
    FileStatResponse resp = GetFileStat(NULL);
    // Undefined behavior, test for robustness
    ck_assert_int_ne(resp.error, ERR_OK); // Assume it fails
}
END_TEST


// Reader tests

// Pool Lifecycle Tests
START_TEST(test_create_file_reader_pool)
{
    ReaderPoolParams params = {10, 2};
    FileReaderPool *pool = CreateFileReaderPool(&params);
    ck_assert_ptr_nonnull(pool);
    ShutdownFileReaderPool(pool);
    DestroyFileReaderPool(pool);
}
END_TEST

START_TEST(test_create_file_reader_pool_invalid_params)
{
    ReaderPoolParams params = {0, 0}; // Invalid
    FileReaderPool *pool = CreateFileReaderPool(&params);
    ck_assert_ptr_null(pool);
}
END_TEST

START_TEST(test_shutdown_file_reader_pool_with_pending)
{
    ReaderPoolParams params = {10, 2};
    FileReaderPool *pool = CreateFileReaderPool(&params);
    ck_assert_ptr_nonnull(pool);

    // Queue a request
    char buffer[100];
    FileReadRequest req = {
        .path = "testdata/test.txt",
        .buffer = buffer,
        .bufferSize = sizeof(buffer),
        .callback = test_callback,
        .userData = NULL
    };
    FileReadSet set = QueueFile(pool, req);
    ck_assert_int_eq(set.error, ERR_OK);

    // Shutdown immediately
    int result = ShutdownFileReaderPool(pool);
    ck_assert_int_eq(result, ERR_OK);

    // Check that request was canceled
    wait_for_responses(1);
    ck_assert_ptr_nonnull(responses[0]);
    ck_assert_int_eq(responses[0]->error, ERR_REQUEST_CANCELED);

    reset_responses();

    DestroyFileReaderPool(pool);
}
END_TEST

START_TEST(test_graceful_shutdown_file_reader_pool)
{
    ReaderPoolParams params = {10, 2};
    FileReaderPool *pool = CreateFileReaderPool(&params);
    ck_assert_ptr_nonnull(pool);

    // Queue a request
    char buffer[100];
    FileReadRequest req = {
        .path = "testdata/test.txt",
        .buffer = buffer,
        .bufferSize = sizeof(buffer),
        .callback = test_callback,
        .userData = NULL
    };
    FileReadSet set = QueueFile(pool, req);
    ck_assert_int_eq(set.error, ERR_OK);

    // Graceful shutdown
    int result = GracefullyShutdownFileReaderPool(pool);
    ck_assert_int_eq(result, ERR_OK);

    // Wait for completion
    wait_for_responses(1);
    ck_assert_ptr_nonnull(responses[0]);
    ck_assert_int_eq(responses[0]->error, ERR_OK);

    reset_responses();

    DestroyFileReaderPool(pool);
}
END_TEST

START_TEST(test_destroy_file_reader_pool)
{
    ReaderPoolParams params = {10, 2};
    FileReaderPool *pool = CreateFileReaderPool(&params);
    ck_assert_ptr_nonnull(pool);
    ShutdownFileReaderPool(pool);
    DestroyFileReaderPool(pool);
    // No assertions, just ensure no crash
}
END_TEST

// Queue and Request Tests
START_TEST(test_queue_file_success)
{
    ReaderPoolParams params = {10, 2};
    FileReaderPool *pool = CreateFileReaderPool(&params);
    ck_assert_ptr_nonnull(pool);

    char buffer[100];
    FileReadRequest req = {
        .path = "testdata/test.txt",
        .buffer = buffer,
        .bufferSize = sizeof(buffer),
        .callback = test_callback,
        .userData = NULL
    };

    FileReadSet set = QueueFile(pool, req);
    ck_assert_int_eq(set.error, ERR_OK);

    // Wait for response
    wait_for_responses(1);
    ck_assert_ptr_nonnull(responses[0]);
    ck_assert_int_eq(responses[0]->error, ERR_OK);
    ck_assert_int_eq(responses[0]->bytesRead, 12);
    buffer[responses[0]->bytesRead] = '\0'; // Null-terminate
    ck_assert_str_eq(buffer, "Hello World\n");

    ShutdownFileReaderPool(pool);

    reset_responses();
    DestroyFileReaderPool(pool);
}
END_TEST

START_TEST(test_queue_file_not_found)
{
    ReaderPoolParams params = {10, 2};
    FileReaderPool *pool = CreateFileReaderPool(&params);
    ck_assert_ptr_nonnull(pool);

    char buffer[100];
    FileReadRequest req = {
        .path = "testdata/nonexistent.txt",
        .buffer = buffer,
        .bufferSize = sizeof(buffer),
        .callback = test_callback,
        .userData = NULL
    };

    FileReadSet set = QueueFile(pool, req);
    ck_assert_int_eq(set.error, ERR_OK);

    // Wait for response
    wait_for_responses(1);
    ck_assert_ptr_nonnull(responses[0]);
    ck_assert_int_eq(responses[0]->error, ERR_FILE_NOT_FOUND);


    ShutdownFileReaderPool(pool);

    reset_responses();
    DestroyFileReaderPool(pool);
}
END_TEST

START_TEST(test_queue_file_null_buffer)
{
    ReaderPoolParams params = {10, 2};
    FileReaderPool *pool = CreateFileReaderPool(&params);
    ck_assert_ptr_nonnull(pool);

    FileReadRequest req = {
        .path = "testdata/test.txt",
        .buffer = NULL,
        .bufferSize = 100,
        .callback = test_callback,
        .userData = NULL
    };

    FileReadSet set = QueueFile(pool, req);
    ck_assert_int_eq(set.error, ERR_INVALID_PARAMETER);

    ShutdownFileReaderPool(pool);

    reset_responses();
    DestroyFileReaderPool(pool);
}
END_TEST

START_TEST(test_queue_file_zero_buffer_size)
{
    ReaderPoolParams params = {10, 2};
    FileReaderPool *pool = CreateFileReaderPool(&params);
    ck_assert_ptr_nonnull(pool);

    char buffer[100];
    FileReadRequest req = {
        .path = "testdata/test.txt",
        .buffer = buffer,
        .bufferSize = 0,
        .callback = test_callback,
        .userData = NULL
    };

    FileReadSet set = QueueFile(pool, req);
    ck_assert_int_eq(set.error, ERR_INVALID_PARAMETER);

    ShutdownFileReaderPool(pool);

    reset_responses();
    DestroyFileReaderPool(pool);
}
END_TEST

START_TEST(test_queue_file_null_callback)
{
    ReaderPoolParams params = {10, 2};
    FileReaderPool *pool = CreateFileReaderPool(&params);
    ck_assert_ptr_nonnull(pool);

    char buffer[100];
    FileReadRequest req = {
        .path = "testdata/test.txt",
        .buffer = buffer,
        .bufferSize = sizeof(buffer),
        .callback = NULL,
        .userData = NULL
    };

    FileReadSet set = QueueFile(pool, req);
    ck_assert_int_eq(set.error, ERR_INVALID_PARAMETER);

    ShutdownFileReaderPool(pool);

    reset_responses();
    DestroyFileReaderPool(pool);
}
END_TEST

START_TEST(test_queue_file_null_path)
{
    ReaderPoolParams params = {10, 2};
    FileReaderPool *pool = CreateFileReaderPool(&params);
    ck_assert_ptr_nonnull(pool);

    char buffer[100];
    FileReadRequest req = {
        .path = NULL,
        .buffer = buffer,
        .bufferSize = sizeof(buffer),
        .callback = test_callback,
        .userData = NULL
    };

    FileReadSet set = QueueFile(pool, req);
    ck_assert_int_eq(set.error, ERR_INVALID_PARAMETER);

    ShutdownFileReaderPool(pool);

    reset_responses();
    DestroyFileReaderPool(pool);
}
END_TEST

START_TEST(test_queue_file_after_shutdown)
{
    ReaderPoolParams params = {10, 2};
    FileReaderPool *pool = CreateFileReaderPool(&params);
    ck_assert_ptr_nonnull(pool);
    ShutdownFileReaderPool(pool);

    char buffer[100];
    FileReadRequest req = {
        .path = "testdata/test.txt",
        .buffer = buffer,
        .bufferSize = sizeof(buffer),
        .callback = test_callback,
        .userData = NULL
    };

    FileReadSet set = QueueFile(pool, req);
    ck_assert_int_eq(set.error, ERR_SHUTDOWN);

    DestroyFileReaderPool(pool);
}
END_TEST

START_TEST(test_queue_file_max_requests_exceeded)
{
    ReaderPoolParams params = {1, 1}; // max_requests = 1
    FileReaderPool *pool = CreateFileReaderPool(&params);
    ck_assert_ptr_nonnull(pool);

    char buffer[100];
    FileReadRequest req = {
        .path = "testdata/test.txt",
        .buffer = buffer,
        .bufferSize = sizeof(buffer),
        .callback = test_callback,
        .userData = NULL
    };

    // Queue first request
    FileReadSet set1 = QueueFile(pool, req);
    ck_assert_int_eq(set1.error, ERR_OK);

    // Queue second, should exceed
    QueueFile(pool, req);
    FileReadSet set3 = QueueFile(pool, req);

    ck_assert_int_eq(set3.error, ERR_MAX_REQUESTS_EXCEEDED);

    ShutdownFileReaderPool(pool);

    reset_responses();

    DestroyFileReaderPool(pool);
}
END_TEST

START_TEST(test_queue_file_large_file)
{
    ReaderPoolParams params = {10, 2};
    FileReaderPool *pool = CreateFileReaderPool(&params);
    ck_assert_ptr_nonnull(pool);

    char buffer[5]; // Small buffer
    FileReadRequest req = {
        .path = "testdata/test.txt", // 12 bytes
        .buffer = buffer,
        .bufferSize = sizeof(buffer),
        .callback = test_callback,
        .userData = NULL
    };

    FileReadSet set = QueueFile(pool, req);
    ck_assert_int_eq(set.error, ERR_OK);

    // Wait for response
    wait_for_responses(1);
    ck_assert_ptr_nonnull(responses[0]);
    ck_assert_int_eq(responses[0]->error, ERR_FILE_TOO_LARGE);

    ShutdownFileReaderPool(pool);

    reset_responses();
    DestroyFileReaderPool(pool);
}
END_TEST

START_TEST(test_queue_file_empty_file)
{
    ReaderPoolParams params = {10, 2};
    FileReaderPool *pool = CreateFileReaderPool(&params);
    ck_assert_ptr_nonnull(pool);

    char buffer[100];
    FileReadRequest req = {
        .path = "testdata/empty.txt",
        .buffer = buffer,
        .bufferSize = sizeof(buffer),
        .callback = test_callback,
        .userData = NULL
    };

    FileReadSet set = QueueFile(pool, req);
    ck_assert_int_eq(set.error, ERR_OK);

    // Wait for response
    wait_for_responses(1);
    ck_assert_ptr_nonnull(responses[0]);
    ck_assert_int_eq(responses[0]->error, ERR_OK);
    ck_assert_int_eq(responses[0]->bytesRead, 0);

    ShutdownFileReaderPool(pool);

    reset_responses();
    DestroyFileReaderPool(pool);
}
END_TEST

START_TEST(test_queue_file_binary_file)
{
    ReaderPoolParams params = {10, 2};
    FileReaderPool *pool = CreateFileReaderPool(&params);
    ck_assert_ptr_nonnull(pool);

    // Assume testdata/test.txt is text, but for binary, use it or create binary
    // For simplicity, use test.txt
    char buffer[100];
    FileReadRequest req = {
        .path = "testdata/test.txt",
        .buffer = buffer,
        .bufferSize = sizeof(buffer),
        .callback = test_callback,
        .userData = NULL
    };

    FileReadSet set = QueueFile(pool, req);
    ck_assert_int_eq(set.error, ERR_OK);

    // Wait for response
    wait_for_responses(1);
    ck_assert_ptr_nonnull(responses[0]);
    ck_assert_int_eq(responses[0]->error, ERR_OK);
    ck_assert_int_eq(responses[0]->bytesRead, 12);


    ShutdownFileReaderPool(pool);

    reset_responses();
    DestroyFileReaderPool(pool);
}
END_TEST

// Cancel Tests
START_TEST(test_cancel_file)
{
    ReaderPoolParams params = {10, 2};
    FileReaderPool *pool = CreateFileReaderPool(&params);
    ck_assert_ptr_nonnull(pool);

    char buffer[100];
    FileReadRequest req = {
        .path = "testdata/test2.txt",
        .buffer = buffer,
        .bufferSize = sizeof(buffer),
        .callback = test_callback,
        .userData = NULL
    };

    FileReadSet set = QueueFile(pool, req);
    ck_assert_int_eq(set.error, ERR_OK);

    // Cancel immediately
    int cancel_result = CancelFile(pool, set.request_id);
    ck_assert_int_eq(cancel_result, ERR_OK);

    // Wait for response
    wait_for_responses(1);
    ck_assert_ptr_nonnull(responses[0]);
    ck_assert_int_eq(responses[0]->error, ERR_REQUEST_CANCELED);


    ShutdownFileReaderPool(pool);

    reset_responses();
    DestroyFileReaderPool(pool);
}
END_TEST

START_TEST(test_cancel_file_after_shutdown)
{
    ReaderPoolParams params = {10, 2};
    FileReaderPool *pool = CreateFileReaderPool(&params);
    ck_assert_ptr_nonnull(pool);
    ShutdownFileReaderPool(pool);

    uuid_t fake_id;
    uuid_generate(fake_id);
    int result = CancelFile(pool, fake_id);
    ck_assert_int_eq(result, ERR_SHUTDOWN);

    DestroyFileReaderPool(pool);
}
END_TEST

START_TEST(test_cancel_file_nonexistent)
{
    ReaderPoolParams params = {10, 2};
    FileReaderPool *pool = CreateFileReaderPool(&params);
    ck_assert_ptr_nonnull(pool);

    uuid_t fake_id;
    uuid_generate(fake_id);
    int result = CancelFile(pool, fake_id);
    ck_assert_int_eq(result, ERR_REQUEST_NOT_FOUND);

    ShutdownFileReaderPool(pool);

    reset_responses();
    DestroyFileReaderPool(pool);
}
END_TEST

START_TEST(test_cancel_file_already_completed)
{
    ReaderPoolParams params = {10, 2};
    FileReaderPool *pool = CreateFileReaderPool(&params);
    ck_assert_ptr_nonnull(pool);

    char buffer[100];
    FileReadRequest req = {
        .path = "testdata/test.txt",
        .buffer = buffer,
        .bufferSize = sizeof(buffer),
        .callback = test_callback,
        .userData = NULL
    };

    FileReadSet set = QueueFile(pool, req);
    ck_assert_int_eq(set.error, ERR_OK);

    // Wait for completion
    wait_for_responses(1);
    ck_assert_ptr_nonnull(responses[0]);
    ck_assert_int_eq(responses[0]->error, ERR_OK);

    // Try to cancel completed request
    int cancel_result = CancelFile(pool, set.request_id);
    ck_assert_int_eq(cancel_result, ERR_REQUEST_NOT_FOUND);

    ShutdownFileReaderPool(pool);

    reset_responses();
    DestroyFileReaderPool(pool);
}
END_TEST

START_TEST(test_cancel_file_during_read)
{
    ReaderPoolParams params = {10, 2};
    FileReaderPool *pool = CreateFileReaderPool(&params);
    ck_assert_ptr_nonnull(pool);

    char buffer[100];
    FileReadRequest req = {
        .path = "testdata/test2.txt",
        .buffer = buffer,
        .bufferSize = sizeof(buffer),
        .callback = test_callback,
        .userData = NULL
    };

    FileReadSet set = QueueFile(pool, req);
    ck_assert_int_eq(set.error, ERR_OK);

    // Cancel immediately
    int cancel_result = CancelFile(pool, set.request_id);
    ck_assert_int_eq(cancel_result, ERR_OK);

    // Wait for response
    wait_for_responses(1);
    ck_assert_ptr_nonnull(responses[0]);
    ck_assert_int_eq(responses[0]->error, ERR_REQUEST_CANCELED);


    ShutdownFileReaderPool(pool);

    reset_responses();
    DestroyFileReaderPool(pool);
}
END_TEST

// Stats Tests
START_TEST(test_get_reader_pool_stats)
{
    ReaderPoolParams params = {10, 2};
    FileReaderPool *pool = CreateFileReaderPool(&params);
    ck_assert_ptr_nonnull(pool);

    ReaderPoolStats stats = GetReaderPoolStats(pool);
    ck_assert_int_eq(stats.completed_requests, 0);
    ck_assert_int_eq(stats.failed_requests, 0);
    ck_assert_int_eq(stats.canceled_requests, 0);
    ck_assert_int_eq(stats.total_requests, 0);
    ck_assert_int_eq(stats.pending_requests, 0);

    ShutdownFileReaderPool(pool);
    DestroyFileReaderPool(pool);
}
END_TEST

// Concurrent Tests
typedef struct {
    FileReaderPool *pool;
    int thread_id;
} ThreadData;

void *queue_file_thread(void *arg) {
    ThreadData *data = (ThreadData *)arg;
    char buffer[100];
    FileReadRequest req = {
        .path = "testdata/test.txt",
        .buffer = buffer,
        .bufferSize = sizeof(buffer),
        .callback = test_callback,
        .userData = NULL
    };
    QueueFile(data->pool, req);
    return NULL;
}

START_TEST(test_concurrent_queue_files)
{
    ReaderPoolParams params = {10, 2};
    FileReaderPool *pool = CreateFileReaderPool(&params);
    ck_assert_ptr_nonnull(pool);

    pthread_t threads[5];
    ThreadData data[5];
    for (int i = 0; i < 5; i++) {
        data[i].pool = pool;
        data[i].thread_id = i;
        pthread_create(&threads[i], NULL, queue_file_thread, &data[i]);
    }

    for (int i = 0; i < 5; i++) {
        pthread_join(threads[i], NULL);
    }

    // Wait for all responses
    wait_for_responses(5);
    for (int i = 0; i < 5; i++) {
        ck_assert_ptr_nonnull(responses[i]);
        ck_assert_int_eq(responses[i]->error, ERR_OK);
    }
    ShutdownFileReaderPool(pool);
    reset_responses();
    DestroyFileReaderPool(pool);
}
END_TEST

void *cancel_during_read_thread(void *arg) {
    ThreadData *data = (ThreadData *)arg;
    char buffer[100];
    FileReadRequest req = {
        .path = "testdata/test2.txt",
        .buffer = buffer,
        .bufferSize = sizeof(buffer),
        .callback = test_callback,
        .userData = NULL
    };
    FileReadSet set = QueueFile(data->pool, req);
    // Small delay removed
    CancelFile(data->pool, set.request_id);
    return NULL;
}

START_TEST(test_concurrent_cancel_during_read)
{
    ReaderPoolParams params = {10, 1};
    FileReaderPool *pool = CreateFileReaderPool(&params);
    ck_assert_ptr_nonnull(pool);

    pthread_t threads[3];
    ThreadData data[3];
    for (int i = 0; i < 3; i++) {
        data[i].pool = pool;
        data[i].thread_id = i;
        pthread_create(&threads[i], NULL, cancel_during_read_thread, &data[i]);
    }

    for (int i = 0; i < 3; i++) {
        pthread_join(threads[i], NULL);
    }

    // Wait for all responses
    wait_for_responses(3);
    int err_ok_count = 0;
    int err_canceled_count = 0;
    for (int i = 0; i < 3; i++) {
        ck_assert_ptr_nonnull(responses[i]);
        if (responses[i]->error == ERR_OK) {
            err_ok_count++;
        } else if (responses[i]->error == ERR_REQUEST_CANCELED) {
            err_canceled_count++;
        }
    }
    // At least one request should be canceled
    ck_assert_int_le(err_ok_count, 2);
    ck_assert_int_le(err_canceled_count, 3);
    ck_assert_int_ge(err_ok_count, 0);
    ck_assert_int_gt(err_canceled_count, 0);



    ShutdownFileReaderPool(pool);
    reset_responses();
    DestroyFileReaderPool(pool);
}
END_TEST

void *stats_thread(void *arg) {
    FileReaderPool *pool = (FileReaderPool *)arg;
    for (int i = 0; i < 10; i++) {
        (void)GetReaderPoolStats(pool);
        // Just call, no assert
    }
    return NULL;
}

START_TEST(test_thread_safety_stats)
{
    ReaderPoolParams params = {10, 2};
    FileReaderPool *pool = CreateFileReaderPool(&params);
    ck_assert_ptr_nonnull(pool);

    // Queue some requests
    for (int i = 0; i < 5; i++) {
        char buffer[100];
        FileReadRequest req = {
            .path = "testdata/test.txt",
            .buffer = buffer,
            .bufferSize = sizeof(buffer),
            .callback = test_callback,
            .userData = NULL
        };
        QueueFile(pool, req);
    }

    pthread_t threads[3];
    for (int i = 0; i < 3; i++) {
        pthread_create(&threads[i], NULL, stats_thread, pool);
    }

    for (int i = 0; i < 3; i++) {
        pthread_join(threads[i], NULL);
    }

    // Wait for responses
    wait_for_responses(5);
    for (int i = 0; i < 5; i++) {
        ck_assert_ptr_nonnull(responses[i]);
        ck_assert_int_eq(responses[i]->error, ERR_OK);
    }
    ShutdownFileReaderPool(pool);
    reset_responses();
    DestroyFileReaderPool(pool);
}
END_TEST

void *shutdown_thread(void *arg) {
    FileReaderPool *pool = (FileReaderPool *)arg;
    // Delay removed
    ShutdownFileReaderPool(pool);
    return NULL;
}

START_TEST(test_shutdown_during_operations)
{
    ReaderPoolParams params = {10, 2};
    FileReaderPool *pool = CreateFileReaderPool(&params);
    ck_assert_ptr_nonnull(pool);

    // Queue requests
    for (int i = 0; i < 3; i++) {
        char buffer[100];
        FileReadRequest req = {
            .path = "testdata/test.txt",
            .buffer = buffer,
            .bufferSize = sizeof(buffer),
            .callback = test_callback,
            .userData = NULL
        };
        QueueFile(pool, req);
    }

    pthread_t shutdown_t;
    pthread_create(&shutdown_t, NULL, shutdown_thread, pool);

    pthread_join(shutdown_t, NULL);

    // Wait for responses, some may be canceled
    wait_for_responses(3);
    for (int i = 0; i < 3; i++) {
        ck_assert_ptr_nonnull(responses[i]);
        // May be OK or CANCELED
        ck_assert(responses[i]->error == ERR_OK || responses[i]->error == ERR_REQUEST_CANCELED);
    }
    reset_responses();

    DestroyFileReaderPool(pool);
}
END_TEST

Suite *reader_suite(void)
{
    Suite *s = suite_create("Reader");

    TCase *tc_stat = tcase_create("Stat");
    tcase_add_test(tc_stat, test_get_file_stat_existing_file);
    tcase_add_test(tc_stat, test_get_file_stat_nonexistent_file);
    tcase_add_test(tc_stat, test_get_file_stat_directory);
    tcase_add_test(tc_stat, test_get_file_stat_null_path);
    suite_add_tcase(s, tc_stat);

    TCase *tc_lifecycle = tcase_create("ReaderLifecycle");
    tcase_add_test(tc_lifecycle, test_create_file_reader_pool);
    tcase_add_test(tc_lifecycle, test_create_file_reader_pool_invalid_params);
    tcase_add_test(tc_lifecycle, test_shutdown_file_reader_pool_with_pending);
    tcase_add_test(tc_lifecycle, test_graceful_shutdown_file_reader_pool);
    tcase_add_test(tc_lifecycle, test_destroy_file_reader_pool);
    suite_add_tcase(s, tc_lifecycle);

    TCase *tc_operations = tcase_create("ReaderOperations");
    tcase_add_test(tc_operations, test_queue_file_success);
    tcase_add_test(tc_operations, test_queue_file_not_found);
    tcase_add_test(tc_operations, test_queue_file_null_buffer);
    tcase_add_test(tc_operations, test_queue_file_zero_buffer_size);
    tcase_add_test(tc_operations, test_queue_file_null_callback);
    tcase_add_test(tc_operations, test_queue_file_null_path);
    tcase_add_test(tc_operations, test_queue_file_after_shutdown);
    tcase_add_test(tc_operations, test_queue_file_max_requests_exceeded);
    tcase_add_test(tc_operations, test_queue_file_large_file);
    tcase_add_test(tc_operations, test_queue_file_empty_file);
    tcase_add_test(tc_operations, test_queue_file_binary_file);
    tcase_add_test(tc_operations, test_cancel_file);
    tcase_add_test(tc_operations, test_cancel_file_after_shutdown);
    tcase_add_test(tc_operations, test_cancel_file_nonexistent);
    tcase_add_test(tc_operations, test_cancel_file_already_completed);
    tcase_add_test(tc_operations, test_cancel_file_during_read);
    tcase_add_test(tc_operations, test_get_reader_pool_stats);
    suite_add_tcase(s, tc_operations);

    TCase *tc_concurrent = tcase_create("Concurrent");
    tcase_add_test(tc_concurrent, test_concurrent_queue_files);
    tcase_add_test(tc_concurrent, test_concurrent_cancel_during_read);
    tcase_add_test(tc_concurrent, test_thread_safety_stats);
    tcase_add_test(tc_concurrent, test_shutdown_during_operations);
    suite_add_tcase(s, tc_concurrent);

    return s;
}