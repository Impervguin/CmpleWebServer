#ifndef STRING_H__
#define STRING_H__

#include <stddef.h>

typedef struct DynamicString DynamicString;

struct DynamicString {
    char *data;
    size_t size;
    size_t capacity;
};

DynamicString *CreateDynamicString(size_t initial_capacity);
void DestroyDynamicString(DynamicString *string);

int ExpandDynamicString(DynamicString *string, size_t additional_capacity);

int AppendDynamicString(DynamicString *string, const char *data, size_t data_size);
int AppendDynamicStringChar(DynamicString *string, const char *data);

int SetDynamicString(DynamicString *string, const char *data, size_t data_size);
int SetDynamicStringChar(DynamicString *string, const char *data);

int PrefixDynamicString(DynamicString *string, const char *prefix, size_t prefix_size);
int PrefixDynamicStringChar(DynamicString *string, const char *prefix);

#define ERR_OK 0
#define ERR_STRING_MEMORY 1
#define ERR_STRING_NOT_NULL_TERMINATED 2

#endif // STRING_H__