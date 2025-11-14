#ifndef HASH_H__
#define HASH_H__

#include <stddef.h>

unsigned long hash(const char *key, size_t table_size);

#endif // HASH_H__