#include "utils/content.h"

ContentType GetContentType(const char *path) {
    if (path == NULL) return CONTENT_TYPE_TEXT_PLAIN;
    
    const char *ext = strrchr(path, '.');
    if (ext == NULL) return CONTENT_TYPE_TEXT_PLAIN;
    
    for (size_t i = 0; i < content_type_map_size; i++) {
        if (strcmp(ext, content_type_map[i].extension) == 0) {
            return content_type_map[i].type;
        }
    }
    
    return CONTENT_TYPE_APPLICATION_OCTET_STREAM;
}

const char *GetContentTypeString(ContentType content_type) {
    for (size_t i = 0; i < content_type_map_size; i++) {
        if (content_type_map[i].type == content_type) {
            return content_type_map[i].mime_string;
        }
    }
    
    return APPLICATION_OCTET_STREAM_CONTENT_TYPE;
}



const char *ContentTypeByPath(const char *path) {
    return GetContentTypeString(GetContentType(path));
}