#define _GNU_SOURCE
#include "server/server.h"
#include "server/consts.h"
#include "server/errors.h"
#include "server/worker.h"
#include "server/request.h"
#include "reader/reader.h"
#include "cache/cache.h"
#include "utils/log.h"

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
    LogInfo("Creating server...");

    if (params == NULL) {
        LogError("CreateServer: params == NULL");
        return NULL;
    }
    if (params->static_root == NULL) {
        LogError("CreateServer: static_root == NULL");
        return NULL;
    }
    if (params->port == 0) {
        LogError("CreateServer: port == 0");
        return NULL;
    }

    Server *server = malloc(sizeof(Server));
    if (server == NULL) {
        LogError("CreateServer: malloc(Server) failed");
        return NULL;
    }

    memset(server, 0, sizeof(Server));
    server->running = 0;
    server->shutdown = 0;
    server->listenfd = -1;

    server->listen_addr.sin_family = AF_INET;
    server->listen_addr.sin_port = htons(params->port);
    server->listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    LogInfoF("Server params: port=%d, workers=%zu", params->port, params->worker_count);

    ReaderPoolParams reader_pool_params;
    reader_pool_params.max_requests = params->max_requests;
    reader_pool_params.worker_count = params->reader_count;
    
    pthread_mutex_init(&server->mutex, NULL);

    server->reader_pool = CreateFileReaderPool(&reader_pool_params);
    if (server->reader_pool == NULL) {
        LogError("Failed to create FileReaderPool");
        free(server);
        return NULL;
    }
    
    CacheParams cache_manager_params;
    cache_manager_params.max_memory = params->max_cache_size;
    cache_manager_params.max_entries = params->max_cache_entries;
    cache_manager_params.max_buffer_size = params->max_cache_entry_size;

    server->cache_manager = CreateCacheManager(&cache_manager_params);
    if (server->cache_manager == NULL) {
        LogError("Failed to create CacheManager");
        DestroyFileReaderPool(server->reader_pool);
        free(server);
        return NULL;
    }
    
    server->worker_count = params->worker_count;
    server->workers = malloc(sizeof(Worker *) * params->worker_count);
    if (server->workers == NULL) {
        LogError("malloc for workers failed");
        DestroyCacheManager(server->cache_manager);
        DestroyFileReaderPool(server->reader_pool);
        free(server);
        return NULL;
    }
    
    for (size_t i = 0; i < params->worker_count; i++) {
        WorkerParams worker_params;
        worker_params.static_root = params->static_root;
        worker_params.max_requests = params->max_requests;
        worker_params.cache_manager = server->cache_manager;
        worker_params.reader_pool = server->reader_pool;

        Worker *worker = CreateWorker(&worker_params);
        if (worker == NULL) {
            LogErrorF("Failed to create worker #%zu", i);
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
    }

    LogInfo("Server created successfully");
    return server;
}

void DestroyServer(Server *server) {
    if (server == NULL) {
        LogWarn("DestroyServer: server == NULL");
        return;
    }

    LogInfo("Destroying server...");

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

    LogInfo("Server destroyed");
}

int StartServer(Server *server) {
    if (server == NULL) {
        LogError("StartServer: server == NULL");
        return ERR_SERVER_NOT_RUNNING;
    }

    pthread_mutex_lock(&server->mutex);
    if (server->running) {
        pthread_mutex_unlock(&server->mutex);
        LogWarn("Attempted to start the server, but it is already running");
        return ERR_SERVER_ALREADY_RUNNING;
    }

    LogInfo("Starting server...");

    server->listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->listenfd == -1) {
        pthread_mutex_unlock(&server->mutex);
        LogError("socket() failed");
        return ERR_SERVER_MEMORY;
    }

    fcntl(server->listenfd, F_SETFL, O_NONBLOCK);

    int result = bind(server->listenfd, (struct sockaddr *) &server->listen_addr, sizeof(server->listen_addr));
    if (result == -1) {
        pthread_mutex_unlock(&server->mutex);
        LogError("bind() failed");
        return ERR_SERVER_MEMORY;
    }

    result = listen(server->listenfd, 1000);
    if (result == -1) {
        pthread_mutex_unlock(&server->mutex);
        LogError("listen() failed");
        return ERR_SERVER_MEMORY;
    }

    LogInfo("Starting workers...");
    for (size_t i = 0; i < server->worker_count; i++) {
        int result = StartWorker(server->workers[i]);
        if (result != ERR_OK) {
            pthread_mutex_unlock(&server->mutex);
            LogErrorF("Failed to start worker #%zu", i);
            return ERR_SERVER_MEMORY;
        }
    }

    server->running = 1;
    pthread_mutex_unlock(&server->mutex);

    LogInfo("Server started, entering main loop");
    _ServerLoop(server);
    return ERR_OK;
}

int ShutdownServer(Server *server) {
    if (server == NULL) {
        LogError("ShutdownServer: server == NULL");
        return ERR_SERVER_NOT_RUNNING;
    }

    LogWarn("Shutting down server...");

    pthread_mutex_lock(&server->mutex);
    if (!server->running) {
        pthread_mutex_unlock(&server->mutex);
        LogWarn("ShutdownServer: server not running");
        return ERR_SERVER_NOT_RUNNING;
    }
    server->shutdown = 1;
    pthread_mutex_unlock(&server->mutex);

    LogInfo("Waiting for listen to shutdown...");
    while (server->running) {
        usleep(SERVER_SLEEP_TIME);
    }

    pthread_mutex_lock(&server->mutex);
    close(server->listenfd);
    ShutdownFileReaderPool(server->reader_pool);
    for (size_t i = 0; i < server->worker_count; i++) {
        ShutdownWorker(server->workers[i]);
    }
    pthread_mutex_unlock(&server->mutex);

    LogInfo("Server stopped");
    return ERR_OK;
}

int GracefullyShutdownServer(Server *server) {
    pthread_mutex_lock(&server->mutex);
    if (server == NULL) {
        LogError("server == NULL");
        return ERR_SERVER_NOT_RUNNING;
    }
    LogWarn("Graceful shutdown of server...");
    server->shutdown = true;

    
    close(server->listenfd);
    GracefullyShutdownFileReaderPool(server->reader_pool);
    for (size_t i = 0; i < server->worker_count; i++) {
        GracefullyShutdownWorker(server->workers[i]);
    }
    pthread_mutex_unlock(&server->mutex);

    LogInfo("Server stopped");
    return ERR_OK;
}

void _ServerLoop(void *arg) {
    Server *server = (Server *)arg;
    LogInfo("Entering server loop");

    while (1) {
        pthread_mutex_lock(&server->mutex);
        if (server->shutdown) {
            LogWarn("Shutdown signal received");
            server->running = 0;
            pthread_mutex_unlock(&server->mutex);
            break;
        }
        pthread_mutex_unlock(&server->mutex);
        int clientfd = accept(server->listenfd, NULL, NULL);
        if (clientfd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(SERVER_SLEEP_TIME);
                continue;
            }
            LogErrorF("accept() failed: %s", strerror(errno));
            server->running = 0;
            break;
        }
        pthread_mutex_lock(&server->mutex);

        LogDebugF("New client connected: fd=%d", clientfd);

        fcntl(clientfd, F_SETFL, fcntl(clientfd, F_GETFL, 0) | O_NONBLOCK);

        Worker *worker = server->workers[server->last_assigned_worker];
        LogDebugF("Assigning client fd=%d to worker #%zu", clientfd, server->last_assigned_worker);

        server->last_assigned_worker = (server->last_assigned_worker + 1) % server->worker_count;

        int result = AddRequest(worker, clientfd);
        if (result != ERR_OK) {
            LogWarnF("AddRequest failed for fd=%d, closing connection", clientfd);
            close(clientfd);
            pthread_mutex_unlock(&server->mutex);
            continue;
        }

        pthread_mutex_unlock(&server->mutex);
    }

    LogInfo("Exiting server loop");
}
