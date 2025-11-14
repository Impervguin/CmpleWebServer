#ifndef REQUEST_H
#define REQUEST_H

#include "utils/string.h"
#include "cache/cache.h"
#include "reader/stat.h"    

#include <stddef.h>
#include <stdbool.h>
#include <time.h>

#define INITITAL_REQUEST_BUFFER_SIZE 1024

typedef enum  {
    HTTP_REQUEST_GET,
    HTTP_REQUEST_HEAD,
    HTTP_REQUEST_UNSUPPORTED
} HttpRequestMethod;

typedef enum {
    HTTP_STATE_CONNECT,
    HTTP_STATE_READ,
    HTTP_STATE_WAITING_FOR_BODY,
    HTTP_STATE_WRITE,
    HTTP_STATE_DONE,
    HTTP_STATE_ERROR
} HttpRequestState;

typedef enum {
    CONTENT_TYPE_TEXT_PLAIN,
    CONTENT_TYPE_TEXT_HTML,
    CONTENT_TYPE_TEXT_CSS,
    
    CONTENT_TYPE_IMAGE_PNG,
    CONTENT_TYPE_IMAGE_JPEG,
    CONTENT_TYPE_IMAGE_GIF,
    CONTENT_TYPE_IMAGE_SVG,
    CONTENT_TYPE_IMAGE_ICO,
    CONTENT_TYPE_APPLICATION_JAVASCRIPT,
    CONTENT_TYPE_APPLICATION_JSON,
} ContentType;


typedef struct ParsedHttpRequest ParsedHttpRequest;
typedef struct HttpResponseHeader HttpResponseHeader;
typedef struct HttpResponse HttpResponse;

struct ParsedHttpRequest {
    HttpRequestMethod method;
    DynamicString *path;
    DynamicString *user_agent;
    DynamicString *host;    
};

struct HttpResponseHeader {
    ContentType content_type;
    time_t date;
    time_t last_modified;
    size_t content_length;
};

struct HttpResponse {
    HttpResponseHeader header;
    bool header_filled;
    DynamicString *header_buffer;
    size_t header_bytes_written;

    ReadBuffer *body;
    size_t body_bytes_written;
};

typedef struct {
    int socketfd;
    HttpRequestState state;
    
    DynamicString *request_buffer;
    bool request_parsed;
    ParsedHttpRequest parsed_request;

    HttpResponse response;
} HttpRequest;



HttpRequest *CreateHttpRequest(int socketfd);
void DestroyHttpRequest(HttpRequest *request);

int ParseHttpRequest(HttpRequest *request);

int FillHttpResponseHeader(HttpRequest *request, FileStatResponse stat);
int PrepareHttpResponseHeader(HttpRequest *request);
int PrepareHttpForbiddenResponse(HttpRequest *request);
int PrepareHttpNotFoundResponse(HttpRequest *request);
int PrepareHttpUnsupportedMethodResponse(HttpRequest *request);

#endif // REQUEST_H