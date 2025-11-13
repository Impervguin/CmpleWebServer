#include <reader/reader.h>
#include <reader/stat.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

char Buffer[1024];

void Callback(FileReadResponse *response, void *userData) {
    (void)userData;
    if (response->error != ERR_OK) {
        fprintf(stderr, "Error: %d\n", response->error);
        return;
    }
    printf("Read %zu bytes\n", response->bytesRead);
    printf("Buffer: %s\n", Buffer);
    free(response);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    FileReaderPool *pool = CreateFileReaderPool(&(ReaderPoolParams) {
        .max_requests = 10,
        .worker_count = 4,
    });

    if (!pool) {
        fprintf(stderr, "Failed to create reader pool\n");
        return 1;
    }

    FileReadSet set = QueueFile(pool, (FileReadRequest) {
        .path = "/home/impervguin/Projects/CmpleWebServer/src/main.c",
        .buffer = Buffer,
        .bufferSize = sizeof(Buffer),
        .callback = Callback,
        .userData = NULL,
    });

    if (set.error != ERR_OK) {
        fprintf(stderr, "Failed to queue file: %d\n", set.error);
        return 1;
    }

    sleep(1);
    ReaderPoolStats stats = GetReaderPoolStats(pool);
    printf("Completed: %zu\n", stats.completed_requests);
    printf("Failed: %zu\n", stats.failed_requests);
    printf("Canceled: %zu\n", stats.canceled_requests);
    printf("Total: %zu\n", stats.total_requests);
    printf("Pending: %zu\n", stats.pending_requests);
    

    ShutdownFileReaderPool(pool);
    DestroyFileReaderPool(pool);
}

