#include "utils/string.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>


DynamicString *CreateDynamicString(size_t initial_capacity) {
    if (initial_capacity == 0) {
        return NULL;
    }
    DynamicString *string = malloc(sizeof(DynamicString));
    if (string == NULL) {
        return NULL;
    }
    string->data = malloc(sizeof(char) * initial_capacity);
    if (string->data == NULL) {
        free(string);
        return NULL;
    }

    string->size = 1;
    string->capacity = initial_capacity;
    string->data[0] = '\0';

    return string;
}

void DestroyDynamicString(DynamicString *string) {
    free(string->data);
    free(string);
}

int ExpandDynamicString(DynamicString *string, size_t additional_capacity) {
    char *new_data = realloc(string->data, string->capacity + additional_capacity);
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
    return ERR_OK;
}

int AppendDynamicStringChar(DynamicString *string, const char *data) {
    if (!IsNullTerminatedString(string)) {
        return ERR_STRING_NOT_NULL_TERMINATED;
    }
    string->size -= 1; // remove null terminator
    int result = AppendDynamicString(string, data, strlen(data) + 1);
    if (result != ERR_OK) {
        string->size += 1; // restore null terminator
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
    return ERR_OK;
}

int SetDynamicStringChar(DynamicString *string, const char *data) {
    return SetDynamicString(string, data, strlen(data) + 1);
}

int IsNullTerminatedString(DynamicString *string) {
    if (string->data[string->size - 1] != '\0') {
        return 0;
    }
    return 1;
}