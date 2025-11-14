#define _GNU_SOURCE
#include "server/worker.h"
#include "server/consts.h"
#include "server/request.h"
#include "server/errors.h"
#include "utils/strutils.h"

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
        return NULL;
    }
    memset(entry, 0, sizeof(HttpRequestListEntry));
    entry->request = request;
    entry->next = next;
    return entry;
}

void _DestroyRequestEntry(HttpRequestListEntry *entry) {
    if (entry == NULL) {
        return;
    }
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
        return NULL;
    }
    if (params->static_root == NULL || params->cache_manager == NULL || params->reader_pool == NULL || params->max_requests == 0) {
        return NULL;
    }
    Worker *worker = malloc(sizeof(Worker));
    if (worker == NULL) {
        return NULL;
    }
    memset(worker, 0, sizeof(Worker));
    char *static_root = strdup(params->static_root);
    if (static_root == NULL) {
        free(worker);
        return NULL;
    }
    if (static_root[strlen(static_root) - 1] == '/') {
        static_root[strlen(static_root) - 1] = '\0';
    }
    printf("Static root: %s\n", static_root);
    worker->static_root = static_root;
    worker->max_requests = params->max_requests;
    worker->cache_manager = params->cache_manager;
    worker->reader_pool = params->reader_pool;


    pthread_mutex_init(&worker->mutex, NULL);
    pthread_cond_init(&worker->not_empty, NULL);
    worker->running = false;
    worker->shutdown = false;

    return worker;
}

void DestroyWorker(Worker *worker) {
    if (worker == NULL) {
        return;
    }
    if (worker->requests != NULL) {
        HttpRequestListEntry *entry = worker->requests;
        while (entry != NULL) {
            HttpRequestListEntry *next = entry->next;
            DestroyHttpRequest(entry->request);
            free(entry);
            entry = next;
        }
    }

    free(worker->static_root);
    pthread_mutex_destroy(&worker->mutex);
    free(worker);
}

int StartWorker(Worker *worker) {
    printf("Worker %zu starting\n", worker->thread);
    if (worker == NULL) {
        return ERR_WORKER_NOT_RUNNING;
    }
    
    pthread_mutex_lock(&worker->mutex);
    if (worker->running) {
        pthread_mutex_unlock(&worker->mutex);
        return ERR_WORKER_ALREADY_RUNNING;
    }

    worker->running = true;
    pthread_create(&worker->thread, NULL, _WorkerLoop, worker);
    pthread_mutex_unlock(&worker->mutex);
    return ERR_OK;
}

int StopWorker(Worker *worker) {
    if (worker == NULL) {
        return ERR_WORKER_NOT_RUNNING;
    }
    pthread_mutex_lock(&worker->mutex);
    if (!worker->running) {
        pthread_mutex_unlock(&worker->mutex);
        return ERR_WORKER_NOT_RUNNING;
    }
    worker->shutdown = true;
    pthread_cond_signal(&worker->not_empty);
    pthread_mutex_unlock(&worker->mutex);

    pthread_join(worker->thread, NULL);
    return ERR_OK;
}

int AddRequest(Worker *worker, int socketfd) {
    if (worker == NULL) {
        return ERR_WORKER_NOT_RUNNING;
    }


    pthread_mutex_lock(&worker->mutex);
    if (worker->shutdown) {
        pthread_mutex_unlock(&worker->mutex);
        return ERR_WORKER_SHUTDOWN;
    }


    if (worker->current_requests >= worker->max_requests) {
        pthread_mutex_unlock(&worker->mutex);
        return ERR_WORKER_MAX_REQUESTS_EXCEEDED;
    }
    HttpRequest *request = CreateHttpRequest(socketfd);
    if (request == NULL) {
        pthread_mutex_unlock(&worker->mutex);
        return ERR_WORKER_MEMORY;
    }


    HttpRequestListEntry *entry = _CreateRequestEntry(request, worker->requests);
    if (entry == NULL) {
        DestroyHttpRequest(request);
        pthread_mutex_unlock(&worker->mutex);
        return ERR_WORKER_MEMORY;
    }
    worker->requests = entry;
    if (worker->current_requests == 0) {
        pthread_cond_signal(&worker->not_empty);
    }

    worker->current_requests++;
    pthread_mutex_unlock(&worker->mutex);

    return ERR_OK;
}

pthread_t GetWorkerThread(Worker *worker) {
    if (worker == NULL) {
        return (pthread_t) NULL;
    }
    pthread_mutex_lock(&worker->mutex);
    pthread_t thread = worker->thread;
    pthread_mutex_unlock(&worker->mutex);
    return thread;
}

int _ConnectRequest(Worker *worker, HttpRequest *request);
int _ReadRequest(Worker *worker, HttpRequest *request);
int _ParseRequest(Worker *worker, HttpRequest *request);
int _WriteRequest(Worker *worker, HttpRequest *request);
int _DoneRequest(Worker *worker, HttpRequest *request);
int _ErrorRequest(Worker *worker, HttpRequest *request);

// Main worker loop with pselect
void *_WorkerLoop(void *arg) {
    Worker *worker = arg;
    fd_set read_fds;
    fd_set write_fds;
    int max_fd = 0;

    while (1) {
        pthread_mutex_lock(&worker->mutex);
        if (worker->shutdown) {
            pthread_mutex_unlock(&worker->mutex);
            break;
        }

        printf("Worker %zu waiting for request\n", worker->thread);

        if (worker->current_requests == 0) {
            pthread_cond_wait(&worker->not_empty, &worker->mutex);
        }

        printf("Worker %zu got request\n", worker->thread);

        if (worker->shutdown) {
            pthread_mutex_unlock(&worker->mutex);
            break;
        }

        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);

        HttpRequestListEntry *entry = worker->requests;
        while (entry != NULL) {
            HttpRequest *request = entry->request;
            
            if (request->state == HTTP_STATE_CONNECT) {
                _ConnectRequest(worker, request);
                printf("Request %d connected\n", request->socketfd);
            }
            if (request->state == HTTP_STATE_READ) {
                printf("Request %d read set\n", request->socketfd);
                FD_SET(request->socketfd, &read_fds);
                if (request->socketfd > max_fd) {
                    max_fd = request->socketfd;
                }
            }
            if (request->state == HTTP_STATE_WRITE) {
                FD_SET(request->socketfd, &write_fds);
                if (request->socketfd > max_fd) {
                    max_fd = request->socketfd;
                }
            }
            entry = entry->next;
        }
        printf("Worker %zu ready to select\n", worker->thread);
        pthread_mutex_unlock(&worker->mutex);
        int ready = pselect(max_fd + 1, &read_fds, &write_fds, NULL, &PSELECT_TIMEOUT, NULL);
        printf("Worker %zu selected %d\n", worker->thread, ready);
        if (ready == -1) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        pthread_mutex_lock(&worker->mutex);

        entry = worker->requests;
        while (entry != NULL) {
            HttpRequest *request = entry->request;
            if (FD_ISSET(request->socketfd, &read_fds)) {
                _ReadRequest(worker, request);
            }
            if (FD_ISSET(request->socketfd, &write_fds)) {
                _WriteRequest(worker, request);
            }
            HttpRequestListEntry *next = entry->next;
            if (request->state == HTTP_STATE_DONE) {
                _DoneRequest(worker, request);
            } else if (request->state == HTTP_STATE_ERROR) {
                _ErrorRequest(worker, request);
            }
            printf("entry next: %p\n", (void *)next);
            entry = next;
        }
        pthread_mutex_unlock(&worker->mutex);
    }
    return NULL;    
}

int _ConnectRequest(Worker *worker, HttpRequest *request) {
    (void) worker;
    if (request->state != HTTP_STATE_CONNECT) {
        return ERR_OK;
    }
    request->state = HTTP_STATE_READ;
    return ERR_OK;
}

int _ReadRequest(Worker *worker, HttpRequest *request) {
    if (request->state != HTTP_STATE_READ) {
        return ERR_OK;
    }

    printf("Request %d reading\n", request->socketfd);

    char *buffer = request->request_buffer->data + request->request_buffer->size;
    ssize_t bytes_read = read(request -> socketfd, buffer, request->request_buffer->capacity - request->request_buffer->size);
    printf("Request %d read %ld bytes\n", request->socketfd, bytes_read);
    if (bytes_read == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return ERR_OK;
        }
        request->state = HTTP_STATE_ERROR;
        return ERR_WORKER_READ_ERROR;
    }
    request->request_buffer->size += bytes_read;
    printf("buffer after read: %s\n", request->request_buffer->data);

    if (request->request_buffer->size == request->request_buffer->capacity) {
        // Expand buffer
        int err = ExpandDynamicString(request->request_buffer, request->request_buffer->capacity);
        if (err != ERR_OK) {
            request->state = HTTP_STATE_ERROR;
            return ERR_HTTP_MEMORY;
        }
    }

    if (strnstr(request->request_buffer->data, "\r\n\r\n", request->request_buffer->size) != NULL) {
        printf("Parsing\n");
        return _ParseRequest(worker, request);
    }

    return ERR_OK;
}

int _AddStaticRoot(Worker *worker, HttpRequest *request) {
    printf("Adding static root: %s\n", worker->static_root);
    int err = PrefixDynamicStringChar(request->parsed_request.path, worker->static_root);
    if (err != ERR_OK) {
        return ERR_HTTP_MEMORY;
    }
    return ERR_OK;
}

typedef struct {
    Worker *worker;
    HttpRequest *request;
    WriteBuffer *buffer;
} ReadFileCallbackData;

void _ReadFileCallback(FileReadResponse *response, void *userData) {
    printf("Read file callback\n");
    ReadFileCallbackData *data = userData;
    Worker *worker = data->worker;
    HttpRequest *request = data->request;
    WriteBuffer *buffer = data->buffer;
    
    if (response->error != ERR_OK) {
        UnlockWriteBuffer(buffer);
        ReleaseWriteBuffer(buffer);
        request->state = HTTP_STATE_ERROR;
        return;
    }
    *buffer->used = response->bytesRead;
    printf("Bytes read: %zu\n", response->bytesRead);
    printf("Buffer Last char: %d\n", buffer->data[response->bytesRead - 1]);
    buffer->data[response->bytesRead - 1] = '\n';
    UnlockWriteBuffer(buffer);
    ReleaseWriteBuffer(buffer);
    
    ReadBuffer *read_buffer = GetBuffer(worker->cache_manager, request->parsed_request.path->data);
    if (read_buffer == NULL) {
        printf("Request %d buffer not found\n", request->socketfd);
        request->state = HTTP_STATE_ERROR;
        return;
    }
    request->response.body = read_buffer;
    request->state = HTTP_STATE_WRITE;
    return;
}

int _ParseRequest(Worker *worker, HttpRequest *request) {
    printf("Parsing request\n");
    if (request->state != HTTP_STATE_READ) {
        return ERR_OK;
    }
    
    int err = ParseHttpRequest(request);
    if (err != ERR_OK) {
        request->state = HTTP_STATE_ERROR;
        return ERR_HTTP_PARSE;
    }
    printf("Parsed request\n");

    if (request->parsed_request.method == HTTP_REQUEST_UNSUPPORTED) {
        int err = PrepareHttpUnsupportedMethodResponse(request);
        if (err != ERR_OK) {
            request->state = HTTP_STATE_ERROR;
            return ERR_HTTP_MEMORY;
        }
        request->state = HTTP_STATE_WRITE;
        return ERR_OK;
    }

    printf("Request path: %s\n", request->parsed_request.path->data);
    printf("Request method: %d\n", request->parsed_request.method);
    printf("Request user agent: %s\n", request->parsed_request.user_agent->data);
    printf("Request host: %s\n", request->parsed_request.host->data);
    
    // Head
    if (strcmp(request->parsed_request.path->data, "/") == 0) {
        int err = AppendDynamicStringChar(request->request_buffer, "index.html");
        if (err != ERR_OK) {
            request->state = HTTP_STATE_ERROR;
            return ERR_HTTP_MEMORY;
        }
    }

    printf("Request path: %s\n", request->parsed_request.path->data);
    
    printf("Adding static root\n");

    err = _AddStaticRoot(worker, request);
    if (err != ERR_OK) {
        request->state = HTTP_STATE_ERROR;
        return ERR_HTTP_MEMORY;
    }

    printf("Path: %s\n", request->parsed_request.path->data);
    printf("Path size: %zu\n", request->parsed_request.path->size);
    printf("Last char: %c\n", request->parsed_request.path->data[request->parsed_request.path->size]);

    FileStatResponse stat = GetFileStat(request->parsed_request.path->data);
    printf("Stat error: %d\n", stat.error);
    if (stat.error != ERR_OK) {
        if (stat.error == ERR_STAT_FILE_NOT_FOUND) {
            err = PrepareHttpNotFoundResponse(request);
            if (err != ERR_OK) {
                request->state = HTTP_STATE_ERROR;
                return ERR_HTTP_MEMORY;
            }
            request->state = HTTP_STATE_WRITE;
            return ERR_OK;
        }
        err = PrepareHttpNotFoundResponse(request);
        if (err != ERR_OK) {
            request->state = HTTP_STATE_ERROR;
            return ERR_HTTP_MEMORY;
        }
        request->state = HTTP_STATE_WRITE;
        return ERR_HTTP_MEMORY;
    }

    err = FillHttpResponseHeader(request, stat);
    if (err != ERR_OK) {
        request->state = HTTP_STATE_ERROR;
        return ERR_HTTP_MEMORY;
    }
    err = PrepareHttpResponseHeader(request);
    if (err != ERR_OK) {
        request->state = HTTP_STATE_ERROR;
        return ERR_HTTP_MEMORY;
    }

    if (request->parsed_request.method == HTTP_REQUEST_HEAD) {
        // Write header
        printf("Request %d only header\n", request->socketfd);
        request->state = HTTP_STATE_WRITE;
        return ERR_OK;
    }

    printf("Request %d writing body\n", request->socketfd);

    // GET
    ReadBuffer *buffer = GetBuffer(worker->cache_manager, request->parsed_request.path->data);
    if (buffer != NULL) {
        printf("Request %d got buffer\n", request->socketfd);
        request->response.body = buffer;
        request->state = HTTP_STATE_WRITE;
        return ERR_OK;
    }

    printf("Request %d not got buffer\n", request->socketfd);

    err = CreateBuffer(worker->cache_manager, request->parsed_request.path->data, stat.file_size);
    if (err != ERR_OK) {
        printf("Request %d buffer not created\n", request->socketfd);
        request->state = HTTP_STATE_ERROR;
        return ERR_HTTP_MEMORY;
    }

    printf("Request %d creating write buffer\n", request->socketfd);

    WriteBuffer *write_buffer = GetWriteBuffer(worker->cache_manager, request->parsed_request.path->data);
    if (write_buffer == NULL) {
        request->state = HTTP_STATE_ERROR;
        return ERR_HTTP_MEMORY;
    }

    printf("Request %d write buffer created\n", request->socketfd);

    LockWriteBuffer(write_buffer);

    if (*write_buffer->used == stat.file_size) { 
        // File is already cached while waiting for write
        UnlockWriteBuffer(write_buffer);
        ReleaseWriteBuffer(write_buffer);
        buffer = GetBuffer(worker->cache_manager, request->parsed_request.path->data);
        if (buffer == NULL) {
            request->state = HTTP_STATE_ERROR;
            return ERR_HTTP_MEMORY;
        }
        request->response.body = buffer;
        request->state = HTTP_STATE_WRITE;
        return ERR_OK;
    }

    printf("Request %d not cached\n", request->socketfd);

    // File is not cached
    FileReadRequest read_request;
    read_request.path = request->parsed_request.path->data;
    read_request.buffer = write_buffer->data;
    read_request.bufferSize = stat.file_size;
    read_request.callback = _ReadFileCallback;
    
    ReadFileCallbackData *callback_data = malloc(sizeof(ReadFileCallbackData));
    if (callback_data == NULL) {
        ReleaseWriteBuffer(write_buffer);
        request->state = HTTP_STATE_ERROR;
        return ERR_HTTP_MEMORY;
    }
    callback_data->worker = worker;
    callback_data->request = request;
    callback_data->buffer = write_buffer;
    read_request.userData = callback_data;
    
    LockWriteBuffer(write_buffer);
    FileReadSet read_set = QueueFile(worker->reader_pool, read_request);
    if (read_set.error != ERR_OK) {
        ReleaseWriteBuffer(write_buffer);
        request->state = HTTP_STATE_ERROR;
        return ERR_HTTP_MEMORY;
    }
    
    request->response.body = buffer;
    request->state = HTTP_STATE_WAITING_FOR_BODY;
    
    return ERR_OK;
}

int _WriteRequest(Worker *worker, HttpRequest *request) {
    (void) worker;
    request->state = HTTP_STATE_WRITE;
    printf("Request %d writing\n", request->socketfd);

    if (request->response.header_buffer == NULL) {
        request->state = HTTP_STATE_ERROR;
        return ERR_HTTP_MEMORY;
    }

    if (request->response.header_bytes_written < request->response.header_buffer->size) {
        printf("Request %d writing header\n", request->socketfd);
        printf("Already written: %zu\n", request->response.header_bytes_written);
        printf("Total size: %zu\n", request->response.header_buffer->size);
        // Write header
        ssize_t bytes_written = write(request->socketfd, request->response.header_buffer->data + request->response.header_bytes_written, request->response.header_buffer->size - request->response.header_bytes_written);
        printf("Request %d wrote %ld bytes\n", request->socketfd, bytes_written);
        if (bytes_written == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return ERR_OK;
            }
            request->state = HTTP_STATE_ERROR;
            return ERR_WORKER_WRITE_ERROR;
        }
        request->response.header_bytes_written += bytes_written;
        printf("Request %d header written\n", request->socketfd);
        return ERR_OK;
    }

    printf("Request %d writing body\n", request->socketfd);

    if (request->response.body == NULL) {
        printf("Request %d done\n", request->socketfd);
        request->state = HTTP_STATE_DONE;
        return ERR_OK;
    }

    printf("Request %d writing body 2\n", request->socketfd);

    LockReadBuffer(request->response.body);
    if (request->response.body_bytes_written < *request->response.body->used) {
        ReadBuffer *read_buffer = request->response.body;
        ssize_t bytes_written = write(request->socketfd, read_buffer->data + request->response.body_bytes_written, *read_buffer->used - request->response.body_bytes_written);
        printf("Request %d wrote %ld bytes\n", request->socketfd, bytes_written);
        if (bytes_written == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                UnlockReadBuffer(request->response.body);
                return ERR_OK;
            }
            request->state = HTTP_STATE_ERROR;
            UnlockReadBuffer(request->response.body);
            return ERR_WORKER_WRITE_ERROR;
        }
        
        request->response.body_bytes_written += bytes_written;
        printf("Request %d body bytes written: %zu\n", request->socketfd, request->response.body_bytes_written);
        printf("Request %d body used: %zu\n", request->socketfd, *request->response.body->used);
        printf("Content length: %zu\n", request->response.header.content_length);
    }
    UnlockReadBuffer(request->response.body);

    if (request->response.body_bytes_written >= *request->response.body->used) {
        request->state = HTTP_STATE_DONE;
        return ERR_OK;
    }
    return ERR_OK;
}

int _DeleteRequest(Worker *worker, HttpRequest *request) {
    HttpRequestListEntry *entry = worker->requests;
    if (entry == NULL) {
        DestroyHttpRequest(request);
        return ERR_OK;
    }

    printf("Request %d deleted\n", request->socketfd);

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
    printf("Request %d done\n", request->socketfd);
    if (request->socketfd != -1) {
        close(request->socketfd);
    }
    _DeleteRequest(worker, request);
    return ERR_OK;
}

int _ErrorRequest(Worker *worker, HttpRequest *request) {
    if (request->socketfd != -1) {
        close(request->socketfd);
    }
    _DeleteRequest(worker, request);
    return ERR_OK;
}

