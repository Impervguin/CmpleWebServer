#define _GNU_SOURCE
#include "server/server.h"
#include "server/consts.h"
#include "server/errors.h"
#include "server/worker.h"
#include "server/request.h"
#include "reader/reader.h"
#include "cache/cache.h"

#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define SERVER_SLEEP_TIME 1000


struct Server {
    pthread_mutex_t mutex;
    FileReaderPool *reader_pool;
    CacheManager *cache_manager;

    Worker **workers;
    size_t worker_count;
    size_t last_assigned_worker;

    int running;
    int shutdown;
    
    int listenfd;
    struct sockaddr_in listen_addr;
};

void _ServerLoop(void *arg);

Server *CreateServer(const ServerParams *params) {
    if (params == NULL) {
        return NULL;
    }
    if (params->static_root == NULL) {
        return NULL;
    }
    if (params->port == 0) {
        return NULL;
    }
    Server *server = malloc(sizeof(Server));
    if (server == NULL) {
        return NULL;
    }
    memset(server, 0, sizeof(Server));
    server->running = 0;
    server->shutdown = 0;
    server->listenfd = -1;
    server->listen_addr.sin_family = AF_INET;
    server->listen_addr.sin_port = htons(params->port);
    server->listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    ReaderPoolParams reader_pool_params;
    reader_pool_params.max_requests = params->max_requests;
    reader_pool_params.worker_count = params->reader_count;

    printf("Reader pool params created\n");

    pthread_mutex_init(&server->mutex, NULL);
    server->reader_pool = CreateFileReaderPool(&reader_pool_params);
    if (server->reader_pool == NULL) {
        free(server);
        return NULL;
    }

    printf("File reader pool created\n");

    CacheParams cache_manager_params;
    cache_manager_params.max_memory = params->max_cache_size;
    cache_manager_params.max_entries = params->max_cache_entries;
    cache_manager_params.max_buffer_size = params->max_cache_entry_size;

    server->cache_manager = CreateCacheManager(&cache_manager_params);
    if (server->cache_manager == NULL) {
        DestroyFileReaderPool(server->reader_pool);
        free(server);
        return NULL;
    }

    printf("Cache manager created\n");

    server->worker_count = params->worker_count;
    server->workers = malloc(sizeof(Worker *) * params->worker_count);
    if (server->workers == NULL) {
        DestroyCacheManager(server->cache_manager);
        DestroyFileReaderPool(server->reader_pool);
        free(server);
        return NULL;
    }

    printf("Workers created\n");

    for (size_t i = 0; i < params->worker_count; i++) {
        printf("Worker %zu creating\n", i);
        WorkerParams worker_params;
        worker_params.static_root = params->static_root;
        worker_params.max_requests = params->max_requests;
        worker_params.cache_manager = server->cache_manager;
        worker_params.reader_pool = server->reader_pool;
        Worker *worker = CreateWorker(&worker_params);
        if (worker == NULL) {
            for (size_t j = 0; j < i; j++) {
                DestroyWorker(server->workers[j]);
            }
            DestroyCacheManager(server->cache_manager);
            DestroyFileReaderPool(server->reader_pool);
            free(server->workers);
            free(server);
            return NULL;
        }
        server->workers[i] = worker;
        printf("Worker %zu created\n", i);
    }
    return server;
}

void DestroyServer(Server *server) {
    if (server == NULL) {
        return;
    }
    if (server->listenfd != -1) {
        close(server->listenfd);
    }
    for (size_t i = 0; i < server->worker_count; i++) {
        DestroyWorker(server->workers[i]);
    }
    DestroyCacheManager(server->cache_manager);
    DestroyFileReaderPool(server->reader_pool);
    free(server->workers);
    free(server);
}

int StartServer(Server *server) {
    if (server == NULL) {
        return ERR_SERVER_NOT_RUNNING;
    }

    pthread_mutex_lock(&server->mutex);
    if (server->running) {
        pthread_mutex_unlock(&server->mutex);
        return ERR_SERVER_ALREADY_RUNNING;
    }

    server->listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->listenfd == -1) {
        pthread_mutex_unlock(&server->mutex);
        return ERR_SERVER_MEMORY;
    }

    fcntl(server->listenfd, F_SETFL, O_NONBLOCK);

    int result = bind(server->listenfd, (struct sockaddr *) &server->listen_addr, sizeof(server->listen_addr));
    if (result == -1) {
        pthread_mutex_unlock(&server->mutex);
        return ERR_SERVER_MEMORY;
    }

    result = listen(server->listenfd, 1000);
    if (result == -1) {
        pthread_mutex_unlock(&server->mutex);
        return ERR_SERVER_MEMORY;
    }

    printf("Starting workers\n");
    printf("Worker count: %zu\n", server->worker_count);

    for (size_t i = 0; i < server->worker_count; i++) {
        printf("Worker %zu starting\n", i);
        int result = StartWorker(server->workers[i]);
        if (result != ERR_OK) {
            pthread_mutex_unlock(&server->mutex);
            return ERR_SERVER_MEMORY;
        }
        printf("Worker %zu started\n", i);
    }

    pthread_mutex_unlock(&server->mutex);
    _ServerLoop(server);
    return ERR_OK;
}

int ShutdownServer(Server *server) {
    if (server == NULL) {
        return ERR_SERVER_NOT_RUNNING;
    }
    pthread_mutex_lock(&server->mutex);
    if (!server->running) {
        pthread_mutex_unlock(&server->mutex);
        return ERR_SERVER_NOT_RUNNING;
    }
    server->shutdown = 1;
    pthread_mutex_unlock(&server->mutex);

    while (server->running) {
        sleep(1);
    }

    pthread_mutex_lock(&server->mutex);
    ShutdownFileReaderPool(server->reader_pool);
    for (size_t i = 0; i < server->worker_count; i++) {
        StopWorker(server->workers[i]);    
    }
    pthread_mutex_lock(&server->mutex);

    pthread_mutex_lock(&server->mutex);
    server->shutdown = 0;
    pthread_mutex_unlock(&server->mutex);

    return ERR_OK;
}

int GracefullyShutdownServer(Server *server) {
    if (server == NULL) {
        return ERR_SERVER_NOT_RUNNING;
    }
    pthread_mutex_lock(&server->mutex);
    if (!server->running) {
        pthread_mutex_unlock(&server->mutex);
        return ERR_SERVER_NOT_RUNNING;
    }
    server->shutdown = 1;
    pthread_mutex_unlock(&server->mutex);

    while (server->running)
    {
        sleep(1);
    }

    pthread_mutex_lock(&server->mutex);
    GracefullyShutdownFileReaderPool(server->reader_pool);
    for (size_t i = 0; i < server->worker_count; i++) {
        StopWorker(server->workers[i]);    
    }
    pthread_mutex_lock(&server->mutex);

    pthread_mutex_unlock(&server->mutex);
    return ERR_OK;
}

void _ServerLoop(void *arg) {
    Server *server = (Server *)arg;
    while (1) {
        pthread_mutex_lock(&server->mutex);
        if (server->shutdown) {
            break;
        }

        int clientfd = accept(server->listenfd, NULL, NULL);
        if (clientfd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                pthread_mutex_unlock(&server->mutex);
                usleep(SERVER_SLEEP_TIME);
                continue;
            }
            break;
        }
        printf("Client connected: %d\n", clientfd);
        fcntl(clientfd, F_SETFL, fcntl(clientfd, F_GETFL, 0) | O_NONBLOCK);
        printf("Client set to non-blocking: %d\n", clientfd);
        Worker *worker = server->workers[server->last_assigned_worker];
        printf("Worker selected: %zu\n", server->last_assigned_worker);
        server->last_assigned_worker = (server->last_assigned_worker + 1) % server->worker_count;
        printf("Worker assigned: %zu\n", server->last_assigned_worker);
        int result = AddRequest(worker, clientfd);
        if (result != ERR_OK) {
            close(clientfd);
            pthread_mutex_unlock(&server->mutex);
            continue;
        }

        pthread_mutex_unlock(&server->mutex);
    }

}