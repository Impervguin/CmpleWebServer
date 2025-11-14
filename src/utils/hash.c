#include "utils/hash.h"

unsigned long djb2_hash(const char *key, size_t table_size) {
    unsigned long hash = 5381;
    int c;

    while ((c = *key++)) {
        hash = ((hash << 5) + hash) + c;
    }

    return hash % table_size;
}

unsigned long hash(const char *key, size_t table_size) {
    return djb2_hash(key, table_size);
}