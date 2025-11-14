#ifndef CACHE_H__
#define CACHE_H__

#include <stddef.h>

typedef struct CacheManager CacheManager;

typedef struct BufferMeta BufferMeta;

struct ReadBuffer {
    const char *data;
    const size_t *size;
    const size_t *used;

    BufferMeta * const meta;
};

struct WriteBuffer {
    char * const data;
    const size_t *size;
    size_t *used;

    BufferMeta * const meta;
};

typedef struct ReadBuffer ReadBuffer;
typedef struct WriteBuffer WriteBuffer;

void LockReadBuffer(ReadBuffer *buffer);
void UnlockReadBuffer(ReadBuffer *buffer);

void LockWriteBuffer(WriteBuffer *buffer);
void UnlockWriteBuffer(WriteBuffer *buffer);

struct CacheParams {
    size_t max_memory;
    size_t max_entries;
    size_t max_buffer_size;
};

typedef struct CacheParams CacheParams;

CacheManager *CreateCacheManager(const CacheParams *params);
void DestroyCacheManager(CacheManager *manager);

int CreateBuffer(CacheManager *manager, const char *key, const size_t bufferSize);

ReadBuffer *GetBuffer(CacheManager *manager, const char *key);
WriteBuffer *GetWriteBuffer(CacheManager *manager, const char *key);

void ReleaseBuffer(ReadBuffer *buffer);
void ReleaseWriteBuffer(WriteBuffer *buffer);


#define ERR_OK 0
#define ERR_MEMORY 1
#define ERR_BUFFER_SIZE_LIMIT 2
#define ERR_MEMORY_LIMIT_EXCEEDED 3
#define ERR_BUFFER_NOT_FOUND 4
#define ERR_BUFFER_COUNT_EXCEEDED 5
#define ERR_KEY_NOT_FOUND 6
#define ERR_BUFFERS_USED 7
#define ERR_BUFFER_REFERENCED 8


#endif // CACHE_H__