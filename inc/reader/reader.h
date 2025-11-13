#ifndef READER_H__
#define READER_H__

#include <stddef.h>
#include <uuid/uuid.h>

typedef struct FileReaderPool FileReaderPool;

typedef struct FileReadRequest FileReadRequest;
typedef struct FileReadSet FileReadSet;
typedef struct FileReadResponse FileReadResponse;
typedef struct ReaderPoolParams ReaderPoolParams;

struct FileReadRequest {
    const char *path;
    char *buffer;
    size_t bufferSize;

    void (*callback)(FileReadResponse *response, void *userData);
    void *userData;
};

struct FileReadSet {
    uuid_t request_id;
    int error;
};

struct FileReadResponse {
    uuid_t request_id;
    const char *path;
    int error;
    size_t bytesRead;
};

struct ReaderPoolParams {
    size_t max_requests;
    size_t worker_count;
};

FileReaderPool *CreateFileReaderPool(const ReaderPoolParams *params);
int ShutdownFileReaderPool(FileReaderPool *pool);
int GracefullyShutdownFileReaderPool(FileReaderPool *pool);
void DestroyFileReaderPool(FileReaderPool *pool);

FileReadSet QueueFile(FileReaderPool *pool, FileReadRequest request);
int CancelFile(FileReaderPool *pool, uuid_t request_id);

typedef struct {
    size_t completed_requests;
    size_t failed_requests;
    size_t canceled_requests;
    size_t total_requests;
    size_t pending_requests;
} ReaderPoolStats;

ReaderPoolStats GetReaderPoolStats(FileReaderPool *pool);


#define ERR_OK 0
#define ERR_MEMORY 1
#define ERR_REQUEST_CANCELED 2
#define ERR_REQUEST_NOT_FOUND 3
#define ERR_SHUTDOWN 4
#define ERR_MAX_REQUESTS_EXCEEDED 5
#define ERR_FILE_NOT_FOUND 6
#define ERR_READING_FILE 7
#define ERR_INVALID_PARAMETER 8
#define ERR_FILE_TOO_LARGE 9
#define ERR_FILE_NOT_REGULAR_FILE 10

#endif // READER_H__