#ifndef REQUEST_H
#define REQUEST_H

#include "utils/string.h"
#include "cache/cache.h"
#include "reader/stat.h"
#include "utils/content.h"    

#include <stddef.h>
#include <stdbool.h>
#include <time.h>

#define INITITAL_REQUEST_BUFFER_SIZE 3192
#define INITITAL_PARSED_BUFFERS_SIZE 1024
#define INITIAL_RESPONSE_HEADER_SIZE 1024

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

typedef struct  {
    DynamicString *request_buffer;
} RawHttpRequest;

typedef struct {
    HttpRequestMethod method;
    DynamicString *path;
    DynamicString *user_agent;
    DynamicString *host;    
} ParsedHttpRequest ;

typedef struct  {
    ContentType content_type;
    time_t date;
    time_t last_modified;
    size_t content_length;
} HttpResponseDataHeader;

typedef struct  {
    ReadBuffer *body;
} HttpResponseDataBody;

typedef struct  {
    HttpResponseDataHeader header;
    HttpResponseDataBody body;
} HttpResponseData;

typedef struct  {
    DynamicString *header_buffer;
    size_t header_bytes_written;
    ReadBuffer *body_buffer;
    size_t body_bytes_written;
} HttpResponseRaw;

typedef struct {
    int socketfd;
    HttpRequestState state;

    RawHttpRequest *raw_request;
    ParsedHttpRequest *parsed_request;

    HttpResponseData *response;
    HttpResponseRaw *raw_response;
} HttpRequest;


HttpRequest *CreateHttpRequest(int socketfd);
void DestroyHttpRequest(HttpRequest *request);

int ParseHttpRequest(HttpRequest *request);
int FillHttpResponseHeader(HttpRequest *request, FileStatResponse stat);
int AddHttpResponseBody(HttpRequest *request, ReadBuffer *body);
int PrepareHttpResponseOk(HttpRequest *request);
int PrepareHttpResponseForbidden(HttpRequest *request);
int PrepareHttpResponseNotFound(HttpRequest *request);
int PrepareHttpResponseUnsupportedMethod(HttpRequest *request);

int ReadRequest(HttpRequest *request);
int WriteRequest(HttpRequest *request);

int AddPathPrefix(HttpRequest *request, const char *prefix);
int ReplacePath(HttpRequest *request, const char *path);

#endif // REQUEST_H