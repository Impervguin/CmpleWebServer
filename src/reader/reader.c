#include <reader/reader.h>
#include <reader/stat.h>

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

typedef struct RequestListEntry RequestListEntry;
typedef struct PendingFile PendingFile;

struct FileReaderPool {
    pthread_mutex_t mutex;
    RequestListEntry *requests;
    size_t request_count;
    size_t pending_tasks;

    size_t max_requests;

    pthread_cond_t not_empty;    

    size_t worker_count;
    pthread_t *workers;
    PendingFile **worker_requests;

    // Stats
    size_t completed_requests;
    size_t failed_requests;
    size_t canceled_requests;
    size_t total_requests;

    int shutdown;
};

struct RequestListEntry {
    uuid_t request_id;
    FileReadRequest request;
    RequestListEntry *next;
};

struct PendingFile {
    uuid_t request_id;
    FileReadRequest request;

    int fd;
    int is_canceled;
};


typedef struct {
    FileReaderPool *pool;
    size_t worker_id;
} WorkerParams;
static void *_FileReaderWorker(void *data);

int _CheckPoolParams(const ReaderPoolParams *params) {
    if (params->max_requests == 0) {
        return ERR_INVALID_PARAMETER;
    }
    if (params->worker_count == 0) {
        return ERR_INVALID_PARAMETER;
    }
    return ERR_OK;
}

FileReaderPool *CreateFileReaderPool(const ReaderPoolParams *params) {
    int result = _CheckPoolParams(params);
    if (result != ERR_OK) {
        return NULL;
    }
    FileReaderPool *pool = malloc(sizeof(FileReaderPool));
    if (!pool) {
        return NULL;
    }
    memset(pool, 0, sizeof(FileReaderPool));
    pool->max_requests = params->max_requests;

    pthread_mutex_init(&pool->mutex, NULL);
    pthread_cond_init(&pool->not_empty, NULL);

    pool->worker_count = params->worker_count;
    pool->workers = malloc(sizeof(pthread_t) * pool->worker_count);
    if (!pool->workers) {
        free(pool);
        return NULL;
    }
    pool->worker_requests = malloc(sizeof(RequestListEntry*) * pool->max_requests);
    if (!pool->worker_requests) {
        free(pool->workers);
        free(pool);
        return NULL;
    }
    memset(pool->worker_requests, 0, sizeof(RequestListEntry*) * pool->max_requests);
    memset(pool->workers, 0, sizeof(pthread_t) * pool->worker_count);

    pthread_mutex_lock(&pool->mutex);
    for (size_t i = 0; i < pool->worker_count; i++) {
        WorkerParams *params = malloc(sizeof(WorkerParams));
        if (!params) {
            for (size_t j = 0; j < i; j++) {
                pthread_cancel(pool->workers[j]);
            }
            pthread_mutex_unlock(&pool->mutex);
            free(pool->workers);
            free(pool);
            return NULL;
        }
        params->pool = pool;
        params->worker_id = i;

        if (pthread_create(&pool->workers[i], NULL, _FileReaderWorker, params)) {
            pthread_mutex_unlock(&pool->mutex);
            free(pool->workers);
            free(pool);
            return NULL;
        }
    }
    pthread_mutex_unlock(&pool->mutex);

    return pool;
}

int _CancelFile(FileReaderPool *pool, uuid_t request_id);
int _CancelPendingFile(FileReaderPool *pool, uuid_t request_id);

int ShutdownFileReaderPool(FileReaderPool *pool) {
    pthread_mutex_lock(&pool->mutex);
    pool->shutdown = 1;
    pthread_cond_broadcast(&pool->not_empty);

    uuid_t *request_ids_to_cancel = malloc(sizeof(uuid_t) * pool->request_count);
    if (!request_ids_to_cancel) {
        pthread_mutex_unlock(&pool->mutex);
        return ERR_MEMORY;
    }

    RequestListEntry *entry = pool->requests;
    for (size_t i = 0; i < pool->request_count; i++) {
        memcpy(&request_ids_to_cancel[i], &entry->request_id, sizeof(uuid_t));
        entry = entry->next;
    }

    for (size_t i = 0; i < pool->request_count; i++) {
        _CancelFile(pool, request_ids_to_cancel[i]);
    }

    free(request_ids_to_cancel);

    for (size_t i = 0; i < pool->worker_count; i++) {
        if (pool->worker_requests[i] != NULL) {
            _CancelPendingFile(pool, pool->worker_requests[i]->request_id);
        }
    }
    pthread_mutex_unlock(&pool->mutex);

    // Wait for all tasks to be done
    for (size_t i = 0; i < pool->worker_count; i++) {
        pthread_join(pool->workers[i], NULL);
    }

    return ERR_OK;
}

int GracefullyShutdownFileReaderPool(FileReaderPool *pool) {
    pthread_mutex_lock(&pool->mutex);
    pool->shutdown = 1;
    pthread_cond_broadcast(&pool->not_empty);
    pthread_mutex_unlock(&pool->mutex);

    // Wait for all tasks to be done
    for (size_t i = 0; i < pool->worker_count; i++) {
        pthread_join(pool->workers[i], NULL);
    }
    return ERR_OK;
}

void DestroyFileReaderPool(FileReaderPool *pool) {
    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->not_empty);

    free(pool->worker_requests);
    free(pool->workers);
    free(pool);
}

int _SendCancel(uuid_t request_id, FileReadRequest request) {
    FileReadResponse *response = malloc(sizeof(FileReadResponse));
    if (!response) {
        return ERR_MEMORY;
    }
    memset(response, 0, sizeof(FileReadResponse));
    memcpy(&response->request_id, request_id, sizeof(uuid_t));
    response->error = ERR_REQUEST_CANCELED;
    request.callback(response, request.userData);
    return ERR_OK;
}

// Assumed mutex is locked by calling side
int _CancelFile(FileReaderPool *pool, uuid_t request_id) {
    RequestListEntry *entry = pool->requests;
    if (entry == NULL) {
        return ERR_REQUEST_NOT_FOUND;
    }
    if (uuid_compare(request_id, entry->request_id) == 0) {
        int result = _SendCancel(entry->request_id, entry->request);
        if (!result) {
            return result;
        }
        pool->canceled_requests++;
        pool->request_count--;
        pool->pending_tasks--;
        pool->requests = entry->next;
        free(entry);
        return ERR_OK;
    }

    while (entry->next != NULL) {
        if (uuid_compare(request_id, entry->next->request_id) == 0) {
            int result = _SendCancel(entry->next->request_id, entry->next->request);
            if (!result) {
                return result;
            }
            pool->canceled_requests++;
            pool->request_count--;
            pool->pending_tasks--;
            entry->next = entry->next->next;
            free(entry);
            return ERR_OK;
        }
        entry = entry->next;
    }

    return ERR_REQUEST_NOT_FOUND;
}

// Assumed mutex is locked by calling side
int _CancelPendingFile(FileReaderPool *pool, uuid_t request_id) {
    for (size_t i = 0; i < pool->worker_count; i++) {
        if (pool->worker_requests[i] != NULL && uuid_compare(request_id, pool->worker_requests[i]->request_id) == 0) {
            PendingFile *pending = pool->worker_requests[i];
            // stop worker from reading, it will then cancel the request
            if (pending->fd != -1) {
                close(pending->fd);
            }
            pending->is_canceled = 1;
            return ERR_OK;
        }
    }
    return ERR_REQUEST_NOT_FOUND;
}

int _CheckReadRequest(FileReadRequest request) {
    if (request.buffer == NULL) {
        return ERR_INVALID_PARAMETER;
    }
    if (request.bufferSize == 0) {
        return ERR_INVALID_PARAMETER;
    }
    if (request.callback == NULL) {
        return ERR_INVALID_PARAMETER;
    }
    if (request.path == NULL) {
        return ERR_INVALID_PARAMETER;
    }
    return ERR_OK;
}

FileReadSet QueueFile(FileReaderPool *pool, FileReadRequest request) {
    FileReadSet response;
    int result = _CheckReadRequest(request);
    if (result != ERR_OK) {
        response.error = result;
        return response;
    }
    pthread_mutex_lock(&pool->mutex);
    if (pool->shutdown) {
        pthread_mutex_unlock(&pool->mutex);
        response.error = ERR_SHUTDOWN;
        return response;
    }

    // Check if we have enough requests
    if (pool->request_count >= pool->max_requests) {
        pthread_mutex_unlock(&pool->mutex);
        response.error = ERR_MAX_REQUESTS_EXCEEDED;
        return response;
    }

    // Add request to list
    RequestListEntry *list = pool->requests;

    uuid_t request_id;
    uuid_generate(request_id);
    RequestListEntry *entry = malloc(sizeof(RequestListEntry));
    if (!entry) {
        pthread_mutex_unlock(&pool->mutex);
        response.error = ERR_MEMORY;
        return response;
    }
    memset(entry, 0, sizeof(RequestListEntry));
    memcpy(&entry->request_id, request_id, sizeof(uuid_t));
    entry->request = request;
    entry->next = list;
    pool->requests = entry;
    pool->request_count++;
    pool->pending_tasks++;
    pool->total_requests++;

    // if it is the first request, wake up the workers
    if (list == NULL) {
        pthread_cond_broadcast(&pool->not_empty);
    }

    memcpy(&response.request_id, request_id, sizeof(uuid_t));
    response.error = ERR_OK;
    pthread_mutex_unlock(&pool->mutex);
    return response;
}

int CancelFile(FileReaderPool *pool, uuid_t request_id) {
    pthread_mutex_lock(&pool->mutex);
    if (pool->shutdown) {
        pthread_mutex_unlock(&pool->mutex);
        return ERR_SHUTDOWN;
    }
    int result = _CancelFile(pool, request_id);
    if (result == ERR_REQUEST_NOT_FOUND) {
        result = _CancelPendingFile(pool, request_id);
    }

    pthread_mutex_unlock(&pool->mutex);
    return result;
}

ReaderPoolStats GetReaderPoolStats(FileReaderPool *pool) {
    ReaderPoolStats stats;
    pthread_mutex_lock(&pool->mutex);
    stats.completed_requests = pool->completed_requests;
    stats.failed_requests = pool->failed_requests;
    stats.canceled_requests = pool->canceled_requests;
    stats.total_requests = pool->total_requests;
    stats.pending_requests = pool->pending_tasks;
    pthread_mutex_unlock(&pool->mutex);
    return stats;
}

PendingFile *_TransformEntry(RequestListEntry *entry) {
    PendingFile *pending = malloc(sizeof(PendingFile));
    if (!pending) {
        return NULL;
    }
    memcpy(&pending->request_id, &entry->request_id, sizeof(uuid_t));
    pending->request = entry->request;
    pending->fd = -1;
    pending->is_canceled = 0;
    return pending;
}

int _SendError(uuid_t request_id, FileReadRequest request, int error) {
    FileReadResponse *response = malloc(sizeof(FileReadResponse));
    if (!response) {
        return ERR_MEMORY;
    }
    memset(response, 0, sizeof(FileReadResponse));
    memcpy(&response->request_id, request_id, sizeof(uuid_t));
    response->error = error;
    request.callback(response, request.userData);
    return ERR_OK;
}

int _SendDone(uuid_t request_id, FileReadRequest request, size_t bytesRead) {
    FileReadResponse *response = malloc(sizeof(FileReadResponse));
    if (!response) {
        return ERR_MEMORY;
    }
    memset(response, 0, sizeof(FileReadResponse));
    memcpy(&response->request_id, request_id, sizeof(uuid_t));
    response->error = ERR_OK;
    response->bytesRead = bytesRead;
    request.callback(response, request.userData);
    return ERR_OK;
}

void *_FileReaderWorker(void *data) {
    WorkerParams *params = data;
    FileReaderPool *pool = params->pool;
    size_t worker_id = params->worker_id;
    free(params);

    while (1) {
        pthread_mutex_lock(&pool->mutex);

        if (pool->shutdown && pool->request_count == 0) {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }

        while (!pool->shutdown && pool->request_count == 0) {
            pthread_cond_wait(&pool->not_empty, &pool->mutex);
        }

        // if shutdown is set and no new requests are added, we are done
        if (pool->shutdown && pool->request_count == 0) {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }

        // Extract new request
        RequestListEntry *entry = pool->requests;
        if (entry == NULL) { // Othre workers got requests first
            pthread_mutex_unlock(&pool->mutex);
            continue;
        }
        pool->requests = entry->next;
        pool->request_count--;

        PendingFile *pending = _TransformEntry(entry);
        free(entry);
        if (!pending) {
            _SendError(pending->request_id, pending->request, ERR_MEMORY);
            pool->failed_requests++;
            pool->pending_tasks--;
            pthread_mutex_unlock(&pool->mutex);
            continue;
        }
        pending->fd = open(pending->request.path, O_RDONLY);
        if (pending->fd == -1) {
            _SendError(pending->request_id, pending->request, ERR_FILE_NOT_FOUND);
            pool->failed_requests++;
            pool->pending_tasks--;
            pthread_mutex_unlock(&pool->mutex);
            continue;
        }

        pool->worker_requests[worker_id] = pending;
        pthread_mutex_unlock(&pool->mutex);

        // Check if file is regular file
        FileStatResponse stat_response = GetFileStatFd(pending->fd);
        if (stat_response.error != ERR_OK) {
            pthread_mutex_lock(&pool->mutex);
            if (stat_response.error == ERR_STAT_FILE_NOT_FOUND) {
                _SendError(pending->request_id, pending->request, ERR_FILE_NOT_FOUND);
            } else {
                _SendError(pending->request_id, pending->request, ERR_READING_FILE);
            }
            pool->failed_requests++;
            pool->pending_tasks--;
            pthread_mutex_unlock(&pool->mutex);
            continue;
        }

        if (stat_response.type != RegulatFile) {
            pthread_mutex_lock(&pool->mutex);
            _SendError(pending->request_id, pending->request, ERR_FILE_NOT_REGULAR_FILE);
            pool->failed_requests++;
            pool->pending_tasks--;
            pthread_mutex_unlock(&pool->mutex);
            continue;
        }

        if (pending->request.bufferSize < stat_response.file_size) {
            pthread_mutex_lock(&pool->mutex);
            _SendError(pending->request_id, pending->request, ERR_FILE_TOO_LARGE);
            pool->failed_requests++;
            pool->pending_tasks--;
            pthread_mutex_unlock(&pool->mutex);
            continue;
        }

        // Read from file
        ssize_t bytes_read = read(pending->fd, pending->request.buffer, pending->request.bufferSize);
        
        pthread_mutex_lock(&pool->mutex);
        if (bytes_read == -1) {
            // Task canceled
            if (errno == EBADFD || errno == EBADF) {
                _SendCancel(pending->request_id, pending->request);
                pool->canceled_requests++;
            } else {
                _SendError(pending->request_id, pending->request, ERR_READING_FILE);
                pool->failed_requests++;
            }
            pool->pending_tasks--;
            free(pending);
            pool->worker_requests[worker_id] = NULL;
            pthread_mutex_unlock(&pool->mutex);
            continue;
        }

        if (pending->is_canceled) {
            _SendCancel(pending->request_id, pending->request);
            pool->canceled_requests++;
            pool->pending_tasks--;
            free(pending);
            pool->worker_requests[worker_id] = NULL;
            pthread_mutex_unlock(&pool->mutex);
            continue;
        }

        // Task completed
        _SendDone(pending->request_id, pending->request, bytes_read);
        pool->completed_requests++;
        pool->pending_tasks--;
        free(pending);
        pool->worker_requests[worker_id] = NULL;
        pthread_mutex_unlock(&pool->mutex);
    }
    pthread_exit(NULL);
}
