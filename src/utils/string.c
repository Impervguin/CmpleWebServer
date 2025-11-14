#include "utils/string.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// Always keep capacity at least 1 byte larger than specified
// For null termination
void _MakeNullTerminatedString(DynamicString *string) {
    string->data[string->size] = '\0';
}

DynamicString *CreateDynamicString(size_t initial_capacity) {
    if (initial_capacity == 0) {
        return NULL;
    }
    DynamicString *string = malloc(sizeof(DynamicString));
    if (string == NULL) {
        return NULL;
    }
    string->data = malloc(sizeof(char) * (initial_capacity + 1));
    if (string->data == NULL) {
        free(string);
        return NULL;
    }

    string->capacity = initial_capacity;
    string->size = 0;

    return string;
}

void DestroyDynamicString(DynamicString *string) {
    free(string->data);
    free(string);
}

int ExpandDynamicString(DynamicString *string, size_t additional_capacity) {
    char *new_data = realloc(string->data, string->capacity + additional_capacity + 1);
    if (new_data == NULL) {
        return ERR_STRING_MEMORY;
    }
    string->data = new_data;
    string->capacity += additional_capacity;
    return ERR_OK;
}

int AppendDynamicString(DynamicString *string, const char *data, size_t data_size) {
    if (string->size + data_size > string->capacity) {
        if (ExpandDynamicString(string, string->size + data_size - string->capacity) != ERR_OK) {
            return ERR_STRING_MEMORY;
        }
    }
    memcpy(string->data + string->size, data, data_size);
    string->size += data_size;
    _MakeNullTerminatedString(string);
    return ERR_OK;
}

int AppendDynamicStringChar(DynamicString *string, const char *data) {
    int result = AppendDynamicString(string, data, strlen(data));
    if (result != ERR_OK) {
        return result;
    }
    return ERR_OK;
}

int SetDynamicString(DynamicString *string, const char *data, size_t data_size) {
    if (data_size> string->capacity) {
        if (ExpandDynamicString(string, data_size - string->capacity) != ERR_OK) {
            return ERR_STRING_MEMORY;
        }
    }

    memset(string->data, 0, string->size);
    memcpy(string->data, data, data_size);
    string->size = data_size;
    _MakeNullTerminatedString(string);
    return ERR_OK;
}

int SetDynamicStringChar(DynamicString *string, const char *data) {
    return SetDynamicString(string, data, strlen(data));
}

int PrefixDynamicString(DynamicString *string, const char *prefix, size_t prefix_size) {
    if (prefix_size == 0) {
        return ERR_OK;
    }
    if (string->size + prefix_size > string->capacity) {
        if (ExpandDynamicString(string, prefix_size - string->capacity) != ERR_OK) {
            return ERR_STRING_MEMORY;
        }
    }

    memmove(string->data+prefix_size, string->data, string->size);
    memcpy(string->data, prefix, prefix_size);
    string->size += prefix_size;
    _MakeNullTerminatedString(string);
    return ERR_OK;
}

int PrefixDynamicStringChar(DynamicString *string, const char *prefix) {
    return PrefixDynamicString(string, prefix, strlen(prefix));
}
