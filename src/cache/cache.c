

#define _GNU_SOURCE
#include "cache/cache.h"
#include "utils/hash.h"

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <stdio.h>

struct BufferMeta {
    pthread_mutex_t _mutex;
    pthread_rwlock_t _lock;
    const char *_key;
    unsigned long _hash;

    size_t _reference_count;
    time_t _last_reference_time;
};


BufferMeta *_CreateBufferMeta(const char *key, const size_t bufferSize) {
    BufferMeta *meta = malloc(sizeof(BufferMeta));

    if (meta == NULL) {
        return NULL;
    }

    pthread_rwlock_init(&meta->_lock, NULL);
    pthread_mutex_init(&meta->_mutex, NULL);
    meta->_key = strdup(key);
    if (meta->_key == NULL) {
        pthread_rwlock_destroy(&meta->_lock);
        pthread_mutex_destroy(&meta->_mutex);
        free(meta);
        return NULL;
    }
    meta->_hash = hash(key, bufferSize);
    meta->_reference_count = 0;

    return meta;
}

void _DestroyBufferMeta(BufferMeta *meta) {
    pthread_rwlock_destroy(&meta->_lock);
    pthread_mutex_destroy(&meta->_mutex);
    free(meta);
}

typedef struct CacheBuffer CacheBuffer;

struct CacheBuffer {
    char *data;
    size_t size;
    size_t used;

    BufferMeta *meta;
};

CacheBuffer *_CreateCacheBuffer(const char *key, const size_t bufferSize, const size_t table_size) {
    CacheBuffer *buffer = malloc(sizeof(CacheBuffer));

    if (buffer == NULL) {
        return NULL;
    }

    buffer->data = malloc(bufferSize);

    if (buffer->data == NULL) {
        free(buffer);
        return NULL;
    }

    buffer->size = bufferSize;
    buffer->used = 0;

    buffer->meta = _CreateBufferMeta(key, table_size);

    if (buffer->meta == NULL) {
        free(buffer->data);
        free(buffer);
        return NULL;
    }

    return buffer;
}

void _DestroyCacheBuffer(CacheBuffer *buffer) {
    if (buffer) {
        if (buffer->data != NULL) {
            free(buffer->data);
        }

        if (buffer->meta != NULL) {
            _DestroyBufferMeta(buffer->meta);
        }

        free(buffer);
    }
}

typedef struct HashTableNode HashTableNode;

struct HashTableNode {
    CacheBuffer *buffer;
    HashTableNode *next;
};

HashTableNode *_CreateHashTableNode(CacheBuffer *buffer) {
    HashTableNode *node = malloc(sizeof(HashTableNode));

    if (node == NULL) {
        return NULL;
    }

    node->buffer = buffer;
    node->next = NULL;

    return node;
}

void _DestroyHashTableNode(HashTableNode *node) {
    if (node) {
        if (node->buffer != NULL) {
            _DestroyCacheBuffer(node->buffer);
        }

        free(node);
    }
}

typedef struct CacheManager CacheManager;

struct CacheManager {
    pthread_mutex_t mutex;

    size_t max_memory;
    size_t max_entries;
    size_t max_buffer_size;

    size_t used_memory;
    size_t entry_count;

    HashTableNode **hash_table;
    size_t hash_table_size;
};

CacheManager *CreateCacheManager(const CacheParams *params) {
    CacheManager *manager = malloc(sizeof(CacheManager));

    if (manager == NULL) {
        return NULL;
    }

    pthread_mutex_init(&manager->mutex, NULL);

    manager->max_memory = params->max_memory;
    manager->max_entries = params->max_entries;
    manager->max_buffer_size = params->max_buffer_size;

    manager->used_memory = 0;
    manager->entry_count = 0;

    manager->hash_table = malloc(sizeof(HashTableNode) * manager->max_entries);

    if (manager->hash_table == NULL) {
        free(manager);
        return NULL;
    }

    manager->hash_table_size = manager->max_entries;

    for (size_t i = 0; i < manager->hash_table_size; i++) {
        manager->hash_table[i] = NULL;
    }

    return manager;
}

void DestroyCacheManager(CacheManager *manager) {
    pthread_mutex_destroy(&manager->mutex);

    for (size_t i = 0; i < manager->hash_table_size; i++) {
        HashTableNode *node = manager->hash_table[i];

        while (node != NULL) {
            HashTableNode *next = node->next;
            _DestroyHashTableNode(node);
            node = next;
        }
    }

    free(manager->hash_table);
    free(manager);
}

// Manager must be already locked up to this point.
int _DeleteBuffer(CacheManager *manager, const char *key) {
    const unsigned long key_hash = hash(key, manager->hash_table_size);
    HashTableNode *node = manager->hash_table[key_hash];
    if (node == NULL) {
        return ERR_KEY_NOT_FOUND;
    }
    pthread_mutex_lock(&node->buffer->meta->_mutex);
    if (strcmp(node->buffer->meta->_key, key) == 0) {
        if (node->buffer->meta->_reference_count != 0) {
            pthread_mutex_unlock(&node->buffer->meta->_mutex);
            return ERR_BUFFER_REFERENCED;
        }
        manager->used_memory -= node->buffer->size;
        manager->entry_count--;
        HashTableNode *to_destroy = node;
        manager->hash_table[key_hash] = node->next;
        pthread_mutex_unlock(&to_destroy->buffer->meta->_mutex);
        _DestroyHashTableNode(to_destroy);
        return ERR_OK;
    }
    pthread_mutex_unlock(&node->buffer->meta->_mutex);
    while (node->next != NULL) {
        pthread_mutex_lock(&node->next->buffer->meta->_mutex);
        if (strcmp(node->next->buffer->meta->_key, key) == 0) {
            if (node->next->buffer->meta->_reference_count != 0) {
                pthread_mutex_unlock(&node->next->buffer->meta->_mutex);
                return ERR_BUFFER_REFERENCED;
            }
            manager->used_memory -= node->next->buffer->size;
            manager->entry_count--;
            HashTableNode *to_destroy = node->next;
            node->next = to_destroy->next;
            pthread_mutex_unlock(&to_destroy->buffer->meta->_mutex);
            _DestroyHashTableNode(to_destroy);
            return ERR_OK;
        }
        pthread_mutex_unlock(&node->next->buffer->meta->_mutex);
        node = node->next;
    }
    return ERR_KEY_NOT_FOUND;
}


struct _LRUEntry {
    const char *key;
    time_t last_reference_time;
    size_t buffer_size;
};

int _compare_lru_entries(const void *a, const void *b) {
    const struct _LRUEntry *entryA = a;
    const struct _LRUEntry *entryB = b;
    if (entryA->last_reference_time < entryB->last_reference_time) return -1;
    if (entryA->last_reference_time > entryB->last_reference_time) return 1;
    return 0;
}

// Return array of not used LRU (sorted by last reference time in ascending order) entries
// Manager must be already locker up to this point.
struct _LRUEntry *_FindNotUsedLruEntries(CacheManager *manager, size_t *count) {
    // find count of not used LRU entries
    size_t not_used_count = 0;
    for (size_t i = 0; i < manager->hash_table_size; i++) {
        HashTableNode *node = manager->hash_table[i];
        while (node != NULL) {
            if (node->buffer->meta->_reference_count == 0) {
                not_used_count++;
            }
            node = node->next;
        }
    }

    struct _LRUEntry *lru_keys = malloc(sizeof(struct _LRUEntry) * not_used_count);

    if (lru_keys == NULL) {
        return NULL;
    }

    size_t index = 0;

    // find all entries with lru sorting
    for (size_t i = 0; i < manager->hash_table_size; i++) {
        HashTableNode *node = manager->hash_table[i];
        while (node != NULL) {
            if (node->buffer->meta->_reference_count == 0) {
                lru_keys[index].key = node->buffer->meta->_key;
                lru_keys[index].last_reference_time = node->buffer->meta->_last_reference_time;
                lru_keys[index].buffer_size = node->buffer->size;
                index++;
            }
            node = node->next;
        }
    }
    qsort(lru_keys, not_used_count, sizeof(struct _LRUEntry), _compare_lru_entries);

    *count = not_used_count;
    return lru_keys;
}

// Manager must be already locker up to this point.
int _freeLRUBuffersCount(CacheManager *manager, size_t count) {
    // Find not referenced buffers
    size_t not_used_count = 0;
    struct _LRUEntry *lru_keys = _FindNotUsedLruEntries(manager, &not_used_count);

    // If there are not enough buffers to free - return
    if (not_used_count < count) {
        free(lru_keys);
        return ERR_BUFFERS_USED;
    }

    // If there are enough buffers to free, free <count> least recently used buffers
    size_t freed_count = 0;
    for (size_t i = 0; i < not_used_count; i++) {
        int err = _DeleteBuffer(manager, lru_keys[i].key);
        if (err == ERR_OK) {
            freed_count++;
        }
        if (freed_count == count) {
            break;
        }
    }
    free(lru_keys);
    if (freed_count < count) {
        return ERR_BUFFERS_USED;
    }
    return ERR_OK;
}

int _freeLRUBuffersMemory(CacheManager *manager, size_t memory) {
    // Find not referenced buffers
    size_t not_used_count = 0;
    struct _LRUEntry *lru_keys = _FindNotUsedLruEntries(manager, &not_used_count);

    if (lru_keys == NULL) {
        return ERR_MEMORY;
    }

    // find count of buffers to free
    size_t free_count = 0;
    size_t free_memory = 0;
    for (size_t i = 0; i < not_used_count; i++) {
        free_memory += lru_keys[i].buffer_size;
        free_count++;
        if (free_memory >= memory) {
            break;
        }
    }
    if (free_memory < memory) {
        free(lru_keys);
        return ERR_BUFFERS_USED;
    }
    // If there are enough buffers to free, free <count> least recently used buffers
    size_t freed_count = 0;
    for (size_t i = 0; i < not_used_count; i++) {
        int err = _DeleteBuffer(manager, lru_keys[i].key);
        if (err == ERR_OK) {
            freed_count++;
        }
        if (freed_count >= free_count) {
            break;
        }
    }
    free(lru_keys);
    if (freed_count < free_count) {
        return ERR_BUFFERS_USED;
    }
    return ERR_OK;
}


// Creates with key and specified buffer size
// If buffer size do not fit to max_buffer_size - returns ERR_BUFFER_SIZE_LIMIT
// If cache with this buffer do not fit to max_memory - tries to free least recently used buffers. If there are not enough buffers to free - returns ERR_MEMORY_LIMIT_EXCEEDED
// If buffer count limit is reached - tries to free least recently used buffer. If all buffers are used - returns ERR_BUFFER_COUNT_EXCEEDED
int CreateBuffer(CacheManager *manager, const char *key, const size_t bufferSize) {
    pthread_mutex_lock(&manager->mutex);
    
    if (manager->max_buffer_size < bufferSize) {
        pthread_mutex_unlock(&manager->mutex);
        return ERR_BUFFER_SIZE_LIMIT;
    }

    if (manager->used_memory + bufferSize > manager->max_memory) {
        int err = _freeLRUBuffersMemory(manager, bufferSize - (manager->max_memory - manager->used_memory));
        if (err != ERR_OK) {
            pthread_mutex_unlock(&manager->mutex);
            return ERR_MEMORY_LIMIT_EXCEEDED;
        }
        if (manager->used_memory + bufferSize > manager->max_memory) {
            pthread_mutex_unlock(&manager->mutex);
            return ERR_MEMORY_LIMIT_EXCEEDED;
        }
    }

    if (manager->max_entries <= manager->entry_count) {
        int err = _freeLRUBuffersCount(manager, manager->max_entries - manager->entry_count + 1);
        if (err != ERR_OK) {
            pthread_mutex_unlock(&manager->mutex);
            return ERR_BUFFER_COUNT_EXCEEDED;
        }
        if (manager->max_entries <= manager->entry_count) {
            pthread_mutex_unlock(&manager->mutex);
            return ERR_BUFFER_COUNT_EXCEEDED;
        }
    }

    CacheBuffer *buffer = _CreateCacheBuffer(key, bufferSize, manager->hash_table_size);

    if (buffer == NULL) {
        pthread_mutex_unlock(&manager->mutex);
        return ERR_MEMORY;
    }

    HashTableNode *new = _CreateHashTableNode(buffer);

    if (new == NULL) {
        _DestroyCacheBuffer(buffer);
        pthread_mutex_unlock(&manager->mutex);
        return ERR_MEMORY;
    }

    unsigned long hash = buffer->meta->_hash;

    HashTableNode *node = manager->hash_table[hash];

    if (node == NULL) {
        manager->hash_table[hash] = new;
    } else {
        while (node->next != NULL) {
            node = node->next;
        }
        node->next = new;
    }
    manager->entry_count++;
    manager->used_memory += bufferSize;

    pthread_mutex_unlock(&manager->mutex);
    return ERR_OK;
}

ReadBuffer *_CreateReadBuffer(CacheBuffer *buffer) {
    ReadBuffer *read_buffer = malloc(sizeof(ReadBuffer));
    if (read_buffer == NULL) {
        return NULL;
    }
    ReadBuffer rcb = {
        .data = buffer->data,
        .size = &buffer->size,
        .used = &buffer->used,
        .meta = buffer->meta
    };
    memcpy(read_buffer, &rcb, sizeof(ReadBuffer));

    BufferMeta *meta = buffer->meta;
    
    pthread_mutex_lock(&meta->_mutex);
    
    meta->_reference_count++;
    meta->_last_reference_time = time(NULL);
    pthread_mutex_unlock(&meta->_mutex);

    return read_buffer;
}

ReadBuffer *GetBuffer(CacheManager *manager, const char *key) {
    pthread_mutex_lock(&manager->mutex);
    unsigned long key_hash = hash(key, manager->hash_table_size);
    HashTableNode *node = manager->hash_table[key_hash];
    while (node != NULL) {
        if (strcmp(node->buffer->meta->_key, key) == 0) {
            break;
        }
        node = node->next;
    }
    if (node == NULL) {
        pthread_mutex_unlock(&manager->mutex);
        return NULL;
    }
    ReadBuffer *buffer = _CreateReadBuffer(node->buffer);
    if (buffer == NULL) {
        pthread_mutex_unlock(&manager->mutex);
        return NULL;
    }

    pthread_mutex_unlock(&manager->mutex);
    return buffer;
}

void ReleaseBuffer(ReadBuffer *buffer) {
    BufferMeta *meta = buffer->meta;
    pthread_mutex_lock(&meta->_mutex);
    
    meta->_reference_count--;
    pthread_mutex_unlock(&meta->_mutex);

    free(buffer);
}

WriteBuffer *_CreateWriteBuffer(CacheBuffer *buffer) {
    WriteBuffer *write_buffer = malloc(sizeof(WriteBuffer));
    if (write_buffer == NULL) {
        return NULL;
    }
    WriteBuffer wbc = {
        .data = buffer->data,
        .size = &buffer->size,
        .used = &buffer->used,
        .meta = buffer->meta
    };
    memcpy(write_buffer, &wbc, sizeof(WriteBuffer));

    BufferMeta *meta = buffer->meta;
    pthread_mutex_lock(&meta->_mutex);
    
    meta->_reference_count++;
    meta->_last_reference_time = time(NULL);
    pthread_mutex_unlock(&meta->_mutex);

    return write_buffer;
}

WriteBuffer *GetWriteBuffer(CacheManager *manager, const char *key) {
    pthread_mutex_lock(&manager->mutex);
    unsigned long key_hash = hash(key, manager->hash_table_size);
    HashTableNode *node = manager->hash_table[key_hash];
    while (node != NULL) {
        if (strcmp(node->buffer->meta->_key, key) == 0) {
            break;
        }
        node = node->next;
    }
    if (node == NULL) {
        return NULL;
    }

    WriteBuffer *buffer = _CreateWriteBuffer(node->buffer);
    if (buffer == NULL) {
        pthread_mutex_unlock(&manager->mutex);
        return NULL;
    }

    pthread_mutex_unlock(&manager->mutex);
    return buffer;
}

void ReleaseWriteBuffer(WriteBuffer *buffer) {
    BufferMeta *meta = buffer->meta;
    pthread_mutex_lock(&meta->_mutex);
    meta->_reference_count--;
    pthread_mutex_unlock(&meta->_mutex);

    free(buffer);
}

void LockReadBuffer(ReadBuffer *buffer) {
    pthread_rwlock_rdlock(&buffer->meta->_lock);
}

void UnlockReadBuffer(ReadBuffer *buffer) {
    pthread_rwlock_unlock(&buffer->meta->_lock);
}

void LockWriteBuffer(WriteBuffer *buffer) {
    pthread_rwlock_wrlock(&buffer->meta->_lock);
}

void UnlockWriteBuffer(WriteBuffer *buffer) {
    pthread_rwlock_unlock(&buffer->meta->_lock);
}


