#define _GNU_SOURCE
#include "server/worker.h"
#include "server/consts.h"
#include "server/request.h"
#include "server/errors.h"
#include "utils/strutils.h"
#include "utils/log.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>

static const struct timespec PSELECT_TIMEOUT = {0, 2000};

typedef struct HttpRequestListEntry HttpRequestListEntry;

struct HttpRequestListEntry {
    HttpRequest *request;
    HttpRequestListEntry *next;
};

HttpRequestListEntry *_CreateRequestEntry(HttpRequest *request, HttpRequestListEntry *next) {
    HttpRequestListEntry *entry = malloc(sizeof(HttpRequestListEntry));
    if (entry == NULL) {
        LogError("Failed to allocate HttpRequestListEntry");
        return NULL;
    }
    memset(entry, 0, sizeof(HttpRequestListEntry));
    entry->request = request;
    entry->next = next;
    return entry;
}

void _DestroyRequestEntry(HttpRequestListEntry *entry) {
    if (entry == NULL) return;
    DestroyHttpRequest(entry->request);
    free(entry);
}

struct Worker {
    pthread_mutex_t mutex;
    char *static_root;

    size_t max_requests;
    size_t current_requests;
    HttpRequestListEntry *requests;
    pthread_cond_t not_empty;

    CacheManager *cache_manager;
    FileReaderPool *reader_pool;

    pthread_t thread;
    bool running;
    bool shutdown;
};

void *_WorkerLoop(void *arg);

Worker *CreateWorker(const WorkerParams *params) {
    if (params == NULL) {
        LogError("CreateWorker: params == NULL");
        return NULL;
    }
    if (params->static_root == NULL || params->cache_manager == NULL ||
        params->reader_pool == NULL || params->max_requests == 0) {
        LogError("CreateWorker: invalid parameters");
        return NULL;
    }

    Worker *worker = malloc(sizeof(Worker));
    if (worker == NULL) {
        LogError("Failed to allocate Worker");
        return NULL;
    }

    memset(worker, 0, sizeof(Worker));

    char *static_root = strdup(params->static_root);
    if (static_root == NULL) {
        LogError("Failed to duplicate static_root");
        free(worker);
        return NULL;
    }

    // Normalize root path
    if (static_root[strlen(static_root) - 1] == '/') {
        static_root[strlen(static_root) - 1] = '\0';
    }

    worker->static_root = static_root;
    worker->max_requests = params->max_requests;
    worker->cache_manager = params->cache_manager;
    worker->reader_pool = params->reader_pool;

    pthread_mutex_init(&worker->mutex, NULL);
    pthread_cond_init(&worker->not_empty, NULL);

    LogInfoF("Worker created: max_requests=%zu, static_root=%s",
             worker->max_requests, worker->static_root);

    return worker;
}

void DestroyWorker(Worker *worker) {
    if (worker == NULL) {
        LogWarn("DestroyWorker: worker == NULL");
        return;
    }

    LogInfo("Destroying worker...");

    HttpRequestListEntry *entry = worker->requests;
    while (entry != NULL) {
        HttpRequestListEntry *next = entry->next;
        DestroyHttpRequest(entry->request);
        free(entry);
        entry = next;
    }

    free(worker->static_root);
    pthread_mutex_destroy(&worker->mutex);
    pthread_cond_destroy(&worker->not_empty);
    free(worker);

    LogInfo("Worker destroyed");
}

int StartWorker(Worker *worker) {
    if (worker == NULL) {
        LogError("StartWorker: worker == NULL");
        return ERR_WORKER_NOT_RUNNING;
    }

    pthread_mutex_lock(&worker->mutex);
    if (worker->running) {
        pthread_mutex_unlock(&worker->mutex);
        LogWarn("Attempt to start worker, but it is already running");
        return ERR_WORKER_ALREADY_RUNNING;
    }

    worker->running = true;
    pthread_create(&worker->thread, NULL, _WorkerLoop, worker);

    pthread_mutex_unlock(&worker->mutex);

    LogInfo("Worker thread started");
    return ERR_OK;
}

int GracefullyShutdownWorker(Worker *worker) {
    if (worker == NULL) {
        LogError("worker == NULL");
        return ERR_WORKER_NOT_RUNNING;
    }

    pthread_mutex_lock(&worker->mutex);
    if (!worker->running) {
        pthread_mutex_unlock(&worker->mutex);
        LogWarn("worker not running");
        return ERR_WORKER_NOT_RUNNING;
    }

    LogInfo("Graceful shutdown of worker...");
    worker->shutdown = true;
    pthread_cond_signal(&worker->not_empty);
    pthread_mutex_unlock(&worker->mutex);

    pthread_join(worker->thread, NULL);

    LogInfo("Worker stopped");
    return ERR_OK;
}

int ShutdownWorker(Worker *worker) {
    if (worker == NULL) {
        LogError("worker == NULL");
        return ERR_WORKER_NOT_RUNNING;
    }

    pthread_mutex_lock(&worker->mutex);
    if (!worker->running) {
        pthread_mutex_unlock(&worker->mutex);
        LogWarn("worker not running");
        return ERR_WORKER_NOT_RUNNING;
    }

    LogInfo("Shutdown of worker...");
    worker->shutdown = true;
    pthread_cond_signal(&worker->not_empty);

    HttpRequestListEntry *entry = worker->requests;
    while (entry != NULL) {
        HttpRequestListEntry *next = entry->next;
        _DestroyRequestEntry(entry);
        entry = next;
    }

    pthread_mutex_unlock(&worker->mutex);

    pthread_join(worker->thread, NULL);

    LogInfo("Worker stopped");
    return ERR_OK;
}

int AddRequest(Worker *worker, int socketfd) {
    if (worker == NULL) {
        LogError("AddRequest: worker == NULL");
        return ERR_WORKER_NOT_RUNNING;
    }

    pthread_mutex_lock(&worker->mutex);

    if (worker->shutdown) {
        pthread_mutex_unlock(&worker->mutex);
        LogWarnF("Worker shutting down, cannot accept new request (fd=%d)", socketfd);
        return ERR_WORKER_SHUTDOWN;
    }

    if (worker->current_requests >= worker->max_requests) {
        pthread_mutex_unlock(&worker->mutex);
        LogWarnF("Worker request limit exceeded (%zu/%zu), rejecting fd=%d",
                 worker->current_requests, worker->max_requests, socketfd);
        return ERR_WORKER_MAX_REQUESTS_EXCEEDED;
    }

    HttpRequest *request = CreateHttpRequest(socketfd);
    if (request == NULL) {
        pthread_mutex_unlock(&worker->mutex);
        LogErrorF("Failed to create HttpRequest for fd=%d", socketfd);
        return ERR_WORKER_MEMORY;
    }

    HttpRequestListEntry *entry = _CreateRequestEntry(request, worker->requests);
    if (entry == NULL) {
        DestroyHttpRequest(request);
        pthread_mutex_unlock(&worker->mutex);
        LogError("Failed to allocate request list entry");
        return ERR_WORKER_MEMORY;
    }

    worker->requests = entry;
    worker->current_requests++;

    LogDebugF("Added request fd=%d (total=%zu)", socketfd, worker->current_requests);

    if (worker->current_requests == 1) {
        pthread_cond_signal(&worker->not_empty);
    }

    pthread_mutex_unlock(&worker->mutex);
    return ERR_OK;
}

pthread_t GetWorkerThread(Worker *worker) {
    if (worker == NULL) return (pthread_t) NULL;
    pthread_mutex_lock(&worker->mutex);
    pthread_t thread = worker->thread;
    pthread_mutex_unlock(&worker->mutex);
    return thread;
}

// ─────────────────────────────────────────────────────────────
//  Main Worker Loop
// ─────────────────────────────────────────────────────────────

int _ConnectRequest(Worker *worker, HttpRequest *request);
int _ReadRequest(Worker *worker, HttpRequest *request);
int _ProcessRequest(Worker *worker, HttpRequest *request);
int _WriteRequest(Worker *worker, HttpRequest *request);
int _DeleteRequest(Worker *worker, HttpRequest *request);
int _DoneRequest(Worker *worker, HttpRequest *request);
int _ErrorRequest(Worker *worker, HttpRequest *request);

void *_WorkerLoop(void *arg) {
    Worker *worker = arg;
    LogInfo("Worker loop started");

    fd_set read_fds;
    fd_set write_fds;
    int max_fd = 0;

    while (1) {
        pthread_mutex_lock(&worker->mutex);

        if (worker->shutdown && worker->current_requests == 0) {
            LogWarn("Worker loop interrupted by shutdown");
            pthread_mutex_unlock(&worker->mutex);
            break;
        }

        if (worker->current_requests == 0) {
            pthread_cond_wait(&worker->not_empty, &worker->mutex);
        }

        if (worker->shutdown && worker->current_requests == 0) {
            pthread_mutex_unlock(&worker->mutex);
            break;
        }

        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);
        max_fd = 0;

        HttpRequestListEntry *entry = worker->requests;

        while (entry != NULL) {
            HttpRequest *r = entry->request;

            switch (r->state) {
                case HTTP_STATE_CONNECT:
                    LogDebugF("fd=%d state=CONNECT", r->socketfd);
                    _ConnectRequest(worker, r);
                    break;

                case HTTP_STATE_READ:
                    FD_SET(r->socketfd, &read_fds);
                    if (r->socketfd > max_fd) max_fd = r->socketfd;
                    break;

                case HTTP_STATE_WRITE:
                    FD_SET(r->socketfd, &write_fds);
                    if (r->socketfd > max_fd) max_fd = r->socketfd;
                    break;

                default:
                    break;
            }

            entry = entry->next;
        }

        pthread_mutex_unlock(&worker->mutex);

        int ready = pselect(max_fd + 1, &read_fds, &write_fds, NULL,
                            &PSELECT_TIMEOUT, NULL);

        if (ready == -1) {
            if (errno == EINTR) continue;

            LogErrorF("pselect failed: %s", strerror(errno));
            break;
        }

        pthread_mutex_lock(&worker->mutex);

        entry = worker->requests;
        while (entry != NULL) {
            HttpRequest *r = entry->request;

            if (FD_ISSET(r->socketfd, &read_fds)) {
                LogDebugF("fd=%d: ready to READ", r->socketfd);
                _ReadRequest(worker, r);
            }

            if (FD_ISSET(r->socketfd, &write_fds)) {
                LogDebugF("fd=%d: ready to WRITE", r->socketfd);
                _WriteRequest(worker, r);
            }

            HttpRequestListEntry *next = entry->next;

            if (r->state == HTTP_STATE_DONE) {
                LogInfoF("Request fd=%d completed", r->socketfd);
                _DoneRequest(worker, r);
            } else if (r->state == HTTP_STATE_ERROR) {
                LogWarnF("Request fd=%d completed with ERROR", r->socketfd);
                _ErrorRequest(worker, r);
            }

            entry = next;
        }

        pthread_mutex_unlock(&worker->mutex);
    }

    LogInfo("Worker loop exited");
    return NULL;
}

// ─────────────────────────────────────────────────────────────
//  Request Processors
// ─────────────────────────────────────────────────────────────

int _ConnectRequest(Worker *worker, HttpRequest *request) {
    (void) worker;
    if (request->state != HTTP_STATE_CONNECT) return ERR_OK;
    LogDebugF("fd=%d switching CONNECT → READ", request->socketfd);
    request->state = HTTP_STATE_READ;
    return ERR_OK;
}

int _ReadRequest(Worker *worker, HttpRequest *request) {
    request->state = HTTP_STATE_READ;

    int err = ReadRequest(request);

    if (err == ERR_REQUEST_READ_END) {
        LogDebugF("fd=%d: read complete, parsing...", request->socketfd);
        return _ProcessRequest(worker, request);
    } 
    if (err == ERR_REQUEST_NONBLOCKED_ERROR) {
        return ERR_OK;
    }
    if (err != ERR_OK) {
        LogWarnF("fd=%d: read error", request->socketfd);
        request->state = HTTP_STATE_ERROR;
        return ERR_HTTP_MEMORY;
    }

    return ERR_OK;
}

typedef struct { Worker *worker; HttpRequest *request; WriteBuffer *buffer; } ReadFileCallbackData; 
void _ReadFileCallback(FileReadResponse *response, void *userData) { 
    ReadFileCallbackData *data = userData; 
    HttpRequest *request = data->request; 
    WriteBuffer *buffer = data->buffer; 
    free(data); 
    if (response->error == ERR_OK) { 
        *buffer->used = response->bytesRead;
    } else { 
        // Error occured while reading file 
        // Set Forbidden response 
        int err = PrepareHttpResponseForbidden(request); 
        if (err != ERR_OK) { request->state = HTTP_STATE_ERROR; return; } 
        request->state = HTTP_STATE_WRITE; return; 
    } 
    UnlockWriteBuffer(buffer); 
    ReleaseWriteBuffer(buffer);
    free(response);
    LogDebugF("fd=%d: file read complete successfully", request->socketfd);

    int err = PrepareHttpResponseOk(request);
    if (err != ERR_OK) {
        request->state = HTTP_STATE_ERROR;
        return;
    }
    request->state = HTTP_STATE_WRITE; 
    return; 
}

int _ProcessRequest(Worker *worker, HttpRequest *request) {
    LogDebugF("fd=%d: parsing request", request->socketfd);

    int err = ParseHttpRequest(request);
    if (err == ERR_UNSUPPORTED_HTTP_METHOD ||
        err == ERR_UNSUPPORTED_HTTP_VERSION) {

        LogWarnF("fd=%d: unsupported method/version", request->socketfd);

        err = PrepareHttpResponseUnsupportedMethod(request);
        if (err != ERR_OK) {
            request->state = HTTP_STATE_ERROR;
            return ERR_HTTP_MEMORY;
        }

        request->state = HTTP_STATE_WRITE;
        return ERR_OK;
    }

    if (err != ERR_OK) {
        LogWarnF("fd=%d: parse error", request->socketfd);
        request->state = HTTP_STATE_ERROR;
        return ERR_HTTP_PARSE;
    }

    if (strcmp(request->parsed_request->path->data, "/") == 0) {
        int err = ReplacePath(request, "/index.html");
        if (err != ERR_OK) {
            request->state = HTTP_STATE_ERROR;
            return ERR_HTTP_MEMORY;
        }
    }

    err = AddPathPrefix(request, worker->static_root);
    if (err != ERR_OK) {
        request->state = HTTP_STATE_ERROR;
        return ERR_HTTP_MEMORY;
    }

    LogDebugF("Final path for fd=%d: %s", request->socketfd, request->parsed_request->path->data);

    FileStatResponse stat = GetFileStat(request->parsed_request->path->data);
    if (stat.error == ERR_STAT_FILE_NOT_FOUND) {
        LogWarnF("fd=%d: file not found", request->socketfd);

        err = PrepareHttpResponseNotFound(request);
        if (err != ERR_OK) {
            request->state = HTTP_STATE_ERROR;
            return ERR_HTTP_MEMORY;
        }

        request->state = HTTP_STATE_WRITE;
        return ERR_OK;
    } else if (stat.type != RegulatFile) {
        LogWarnF("fd=%d: file is not a file", request->socketfd);

        err = PrepareHttpResponseForbidden(request);
        if (err != ERR_OK) {
            request->state = HTTP_STATE_ERROR;
            return ERR_HTTP_MEMORY;
        }

        request->state = HTTP_STATE_WRITE;
        return ERR_OK;
    } else if (stat.error != ERR_OK) {
        request->state = HTTP_STATE_ERROR;
        return ERR_HTTP_MEMORY;
    }

    LogDebugF("Filling response header for fd=%d", request->socketfd);
    err = FillHttpResponseHeader(request, stat);
    if (err != ERR_OK) {
        request->state = HTTP_STATE_ERROR;
        return ERR_HTTP_MEMORY;
    }

    // HEAD request
    if (request->parsed_request->method == HTTP_REQUEST_HEAD) {
        LogDebugF("Preparing HEAD response for fd=%d", request->socketfd);
        PrepareHttpResponseOk(request);
        LogDebugF("Writing response for fd=%d", request->socketfd);
        request->state = HTTP_STATE_WRITE;
        return ERR_OK;
    }

    // GET request
    ReadBuffer *buffer = GetBuffer(worker->cache_manager, request->parsed_request->path->data);
    if (buffer != NULL) {
        LogDebugF("fd=%d: cache HIT", request->socketfd);

        AddHttpResponseBody(request, buffer);
        PrepareHttpResponseOk(request);
        request->state = HTTP_STATE_WRITE;
        return ERR_OK;
    }

    LogDebugF("fd=%d: cache MISS", request->socketfd);

    err = CreateBuffer(worker->cache_manager,
                       request->parsed_request->path->data,
                       stat.file_size);
    if (err != ERR_OK) {
        request->state = HTTP_STATE_ERROR;
        return ERR_HTTP_MEMORY;
    }

    WriteBuffer *wb = GetWriteBuffer(worker->cache_manager,
                                     request->parsed_request->path->data);
    if (wb == NULL) {
        request->state = HTTP_STATE_ERROR;
        return ERR_HTTP_MEMORY;
    }

    buffer = GetBuffer(worker->cache_manager, request->parsed_request->path->data);
    if (buffer == NULL) {
        ReleaseWriteBuffer(wb);
        request->state = HTTP_STATE_ERROR;
        return ERR_HTTP_MEMORY;
    }

    AddHttpResponseBody(request, buffer);
    LockWriteBuffer(wb);

    if (*wb->used == stat.file_size) {
        LogDebugF("fd=%d: file already cached", request->socketfd);

        UnlockWriteBuffer(wb);
        ReleaseWriteBuffer(wb);

        PrepareHttpResponseOk(request);
        request->state = HTTP_STATE_WRITE;
        return ERR_OK;
    }

    // Not cached -> read file
    FileReadRequest read_request;
    read_request.path = request->parsed_request->path->data;
    read_request.buffer = wb->data;
    read_request.bufferSize = stat.file_size;
    read_request.callback = _ReadFileCallback;

    ReadFileCallbackData *cbdata = malloc(sizeof(ReadFileCallbackData));
    if (cbdata == NULL) {
        ReleaseWriteBuffer(wb);
        request->state = HTTP_STATE_ERROR;
        return ERR_HTTP_MEMORY;
    }

    cbdata->worker = worker;
    cbdata->request = request;
    cbdata->buffer = wb;
    read_request.userData = cbdata;

    LockWriteBuffer(wb);
    FileReadSet read_set = QueueFile(worker->reader_pool, read_request);
    if (read_set.error != ERR_OK) {
        ReleaseWriteBuffer(wb);
        request->state = HTTP_STATE_ERROR;
        return ERR_HTTP_MEMORY;
    }

    LogDebugF("fd=%d: waiting for file read completion", request->socketfd);

    request->state = HTTP_STATE_WAITING_FOR_BODY;
    return ERR_OK;
}

int _WriteRequest(Worker *worker, HttpRequest *request) {
    (void) worker;
    request->state = HTTP_STATE_WRITE;

    int err = WriteRequest(request);
    if (err == ERR_RESPONSE_WRITE_END) {
        LogDebugF("fd=%d: write complete", request->socketfd);
        request->state = HTTP_STATE_DONE;
        return ERR_OK;
    }
    if (err == ERR_RESPONSE_NONBLOCKED_ERROR) return ERR_OK;

    return ERR_OK;
}

int _DeleteRequest(Worker *worker, HttpRequest *request) {
    HttpRequestListEntry *entry = worker->requests;

    if (entry == NULL) {
        DestroyHttpRequest(request);
        return ERR_OK;
    }

    if (entry->request == request) {
        worker->requests = entry->next;
        _DestroyRequestEntry(entry);
        worker->current_requests--;
        return ERR_OK;
    }

    while (entry->next != NULL) {
        if (entry->next->request == request) {
            HttpRequestListEntry *next = entry->next;
            entry->next = next->next;
            _DestroyRequestEntry(next);
            worker->current_requests--;
            return ERR_OK;
        }
        entry = entry->next;
    }

    return ERR_OK;
}

int _DoneRequest(Worker *worker, HttpRequest *request) {
    LogDebugF("fd=%d: closing connection (DONE)", request->socketfd);
    if (request->socketfd != -1) close(request->socketfd);
    return _DeleteRequest(worker, request);
}

int _ErrorRequest(Worker *worker, HttpRequest *request) {
    LogDebugF("fd=%d: closing connection (ERROR)", request->socketfd);
    if (request->socketfd != -1) close(request->socketfd);
    return _DeleteRequest(worker, request);
}
