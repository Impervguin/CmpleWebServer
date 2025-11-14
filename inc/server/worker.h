#ifndef WORKER_H__
#define WORKER_H__

#include "server/request.h"
#include "cache/cache.h"
#include "reader/reader.h"

#include <pthread.h>

typedef struct Worker Worker;

typedef struct {
    const char *static_root;
    size_t max_requests;
    CacheManager *cache_manager;
    FileReaderPool *reader_pool;
} WorkerParams;

Worker *CreateWorker(const WorkerParams *params);
void DestroyWorker(Worker *worker);

int StartWorker(Worker *worker);
int StopWorker(Worker *worker);

int AddRequest(Worker *worker, int socketfd);
pthread_t GetWorkerThread(Worker *worker);

#endif // WORKER_H__
