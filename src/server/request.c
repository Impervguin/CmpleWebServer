#define _GNU_SOURCE
#include "server/request.h"
#include "server/consts.h"
#include "server/errors.h"
#include "cache/cache.h"
#include "utils/date.h"
#include "utils/strutils.h"
#include "utils/log.h"

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

RawHttpRequest *_CreateRawHttpRequest(void) {
    LogDebug("Creating RawHttpRequest");
    RawHttpRequest *request = malloc(sizeof(RawHttpRequest));
    if (request == NULL) {
        LogError("Failed to allocate RawHttpRequest");
        return NULL;
    }
    request->request_buffer = CreateDynamicString(INITITAL_REQUEST_BUFFER_SIZE);
    if (request->request_buffer == NULL) {
        free(request);
        return NULL;
    }
    return request;
}

ParsedHttpRequest *_CreateParsedHttpRequest(void) {
    LogDebug("Creating ParsedHttpRequest");
    ParsedHttpRequest *request = malloc(sizeof(ParsedHttpRequest));
    if (request == NULL) {
        LogError("Failed to allocate ParsedHttpRequest");
        return NULL;
    }
    request->method = HTTP_REQUEST_UNSUPPORTED;
    request->path = CreateDynamicString(INITITAL_PARSED_BUFFERS_SIZE);
    if (request->path == NULL) {
        free(request);
        return NULL;
    }
    request->user_agent = CreateDynamicString(INITITAL_PARSED_BUFFERS_SIZE);
    if (request->user_agent == NULL) {
        DestroyDynamicString(request->path);
        free(request);
        return NULL;
    }
    request->host = CreateDynamicString(INITITAL_PARSED_BUFFERS_SIZE);
    if (request->host == NULL) {
        DestroyDynamicString(request->user_agent);
        DestroyDynamicString(request->path);
        free(request);
        return NULL;
    }
    return request;
}

HttpResponseData *_CreateHttpResponseData(void) {
    HttpResponseData *response = malloc(sizeof(HttpResponseData));
    if (response == NULL) {
        return NULL;
    }
    response->header.content_type = CONTENT_TYPE_TEXT_PLAIN;
    response->header.date = 0;
    response->header.last_modified = 0;
    response->header.content_length = 0;
    response->body.body = NULL;
    return response;
}

HttpResponseRaw *_CreateHttpResponseRaw(void) {
    HttpResponseRaw *response = malloc(sizeof(HttpResponseRaw));
    if (response == NULL) {
        return NULL;
    }
    response->body_buffer = NULL;
    response->header_buffer = CreateDynamicString(INITIAL_RESPONSE_HEADER_SIZE);
    if (response->header_buffer == NULL) {
        free(response);
        return NULL;
    }
    response->header_bytes_written = 0;
    response->body_bytes_written = 0;
    return response;
}

HttpRequest *CreateHttpRequest(int socketfd) {
    LogDebugF("Creating HttpRequest for socket %d", socketfd);
    HttpRequest *request = malloc(sizeof(HttpRequest));
    if (request == NULL) {
        LogError("Failed to allocate HttpRequest");
        return NULL;
    }
    memset(request, 0, sizeof(HttpRequest));
    request->socketfd = socketfd;
    request->state = HTTP_STATE_CONNECT;
    request->raw_request = _CreateRawHttpRequest();
    if (request->raw_request == NULL) {
        free(request);
        return NULL;
    }

    return request;
}

void _DestroyHttpRequestRaw(RawHttpRequest *request) {
    if (!request) {
        return;
    }
    DestroyDynamicString(request->request_buffer);
    free(request);
}

void _DestroyHttpRequestParsed(ParsedHttpRequest *request) {
    if (!request) {
        return;
    }
    DestroyDynamicString(request->path);
    DestroyDynamicString(request->user_agent);
    DestroyDynamicString(request->host);
    free(request);
}

void _DestroyHttpResponseData(HttpResponseData *response) {
    if (!response) {
        return;
    }
    if (response->body.body != NULL) {
        ReleaseBuffer(response->body.body);
    }
    free(response);
}

void _DestroyHttpResponseRaw(HttpResponseRaw *response) {
    if (!response) {
        return;
    }
    if (response->header_buffer != NULL) {
        DestroyDynamicString(response->header_buffer);
    }
    if (response->body_buffer != NULL) {
        ReleaseBuffer(response->body_buffer);
    }
    free(response);
}

void DestroyHttpRequest(HttpRequest *request) {
    _DestroyHttpRequestRaw(request->raw_request);
    _DestroyHttpRequestParsed(request->parsed_request);
    _DestroyHttpResponseData(request->response);
    _DestroyHttpResponseRaw(request->raw_response);
    free(request);
}

int ParseHttpRequest(HttpRequest *request) {
    LogDebug("Parsing HTTP request");
    if (request->raw_request->request_buffer->size == 0) {
        LogWarn("Request buffer is empty");
        return ERR_OK;
    }

    ParsedHttpRequest *parsed_request = _CreateParsedHttpRequest();
    if (parsed_request == NULL) {
        return ERR_HTTP_MEMORY;
    }

    char *rest = request->raw_request->request_buffer->data;
    char *line;

    // Parse method
    line = strtok_r(rest, "\r\n", &rest);
    if (line == NULL) {
        _DestroyHttpRequestParsed(parsed_request);
        return ERR_HTTP_PARSE;
    }

    char *status_line = strtok_r(line, " ", &line);
    if (line == NULL) {
        _DestroyHttpRequestParsed(parsed_request);
        return ERR_HTTP_PARSE;
    }

    if (strncmp(status_line, "GET", 3) == 0) {
        parsed_request->method = HTTP_REQUEST_GET;
    } else if (strncmp(status_line, "HEAD", 4) == 0) {
        parsed_request->method = HTTP_REQUEST_HEAD;
    } else {
        LogErrorF("Unsupported HTTP method: %s", line);
        _DestroyHttpRequestParsed(parsed_request);
        return ERR_UNSUPPORTED_HTTP_METHOD;
    }

    // Parse path
    status_line = strtok_r(line, " ", &line);
    if (line == NULL) {
        _DestroyHttpRequestParsed(parsed_request);
        return ERR_HTTP_PARSE;
    }
    int err = SetDynamicStringChar(parsed_request->path, status_line);
    if (err != ERR_OK) {
        _DestroyHttpRequestParsed(parsed_request);
        return ERR_HTTP_MEMORY;
    }
    
    // Parse Version
    status_line = strtok_r(line, " ", &line);
    if (line == NULL) {
        _DestroyHttpRequestParsed(parsed_request);
        return ERR_HTTP_PARSE;
    }
    if (strncmp(status_line, "HTTP/1.1", 7) != 0 && strncmp(status_line, "HTTP/1.0", 7) != 0) {
        LogErrorF("Unsupported HTTP version: %s", status_line);
        _DestroyHttpRequestParsed(parsed_request);
        return ERR_UNSUPPORTED_HTTP_VERSION;
    }

    while ((line = strtok_r(rest, "\r\n", &rest)) != NULL) {
        if (strncmp(line, "User-Agent: ", 12) == 0) {
            line = line + 12;
            int err = SetDynamicStringChar(parsed_request->user_agent, line);
            if (err != ERR_OK) {
                _DestroyHttpRequestParsed(parsed_request);
                return ERR_HTTP_MEMORY;
            }
        } else if (strncmp(line, "Host: ", 6) == 0) {
            line = line + 6;
            int err = SetDynamicStringChar(parsed_request->host, line);
            if (err != ERR_OK) {
                _DestroyHttpRequestParsed(parsed_request);
                return ERR_HTTP_MEMORY;
            }
        };
    }
    if (request->parsed_request) {
        _DestroyHttpRequestParsed(request->parsed_request);
    }
    request->parsed_request = parsed_request;
    LogInfoF("Parsed request: method=%d, path=%s", parsed_request->method, parsed_request->path->data);
    return ERR_OK;
}

int FillHttpResponseHeader(HttpRequest *request, FileStatResponse stat) {
    if (!request->parsed_request) {
        return ERR_REQUEST_NOT_PARSED;
    }
    HttpResponseData *response = _CreateHttpResponseData();
    if (response == NULL) {
        return ERR_HTTP_MEMORY;
    }

    response->header.content_type = GetContentType(request->parsed_request->path->data);
    response->header.date = time(NULL);
    response->header.last_modified = stat.last_modified;
    response->header.content_length = stat.file_size;

    if (request->response) {
        _DestroyHttpResponseData(request->response);
    }
    request->response = response;
    return ERR_OK;
}

int AddHttpResponseBody(HttpRequest *request, ReadBuffer *body) {
    if (!request->response) {
        return ERR_RESPONSE_NOT_FILLED;
    }

    HttpResponseData *response = request->response;
    if (response->body.body != NULL) {
        ReleaseBuffer(response->body.body);
    }
    response->body.body = body;
    return ERR_OK;
}

int _WriteStatusLine(HttpResponseRaw *response, const char *version, const char *status);
int _AddHeader(HttpResponseRaw *response, const char *header, const char *value);

int PrepareHttpResponseOk(HttpRequest *request) {
    if (!request->response) {
        return ERR_RESPONSE_NOT_FILLED;
    }
    HttpResponseRaw *raw_response = _CreateHttpResponseRaw();
    if (raw_response == NULL) {
        return ERR_HTTP_MEMORY;
    }

    HttpResponseData *response = request->response;

    LogDebugF("Preparing OK response for fd=%d", request->socketfd);

    // Write status line
    int err = _WriteStatusLine(raw_response, HTTP_ONE_DOT_ONE_VERSION, HTTP_OK_STATUS);
    if (err != ERR_OK) {
        _DestroyHttpResponseRaw(raw_response);
        return err;
    }

    // Add headers

    // Content-Type
    const char *content_type = GetContentTypeString(response->header.content_type);
    if (content_type == NULL) {
        _DestroyHttpResponseRaw(raw_response);
        return ERR_HTTP_PARSE;
    }
    err = _AddHeader(raw_response, HTTP_HEADER_CONTENT_TYPE, content_type);
    if (err != ERR_OK) {
        _DestroyHttpResponseRaw(raw_response);
        return ERR_HTTP_MEMORY;
    }

    // Content-Length
    char content_length_buffer[32];
    int written = snprintf(content_length_buffer, sizeof(content_length_buffer), "%zu", response->header.content_length);
    if (written < 0 || written >= (int)sizeof(content_length_buffer)) {
        _DestroyHttpResponseRaw(raw_response);
        return ERR_HTTP_MEMORY;
    }
    err = _AddHeader(raw_response, HTTP_HEADER_CONTENT_LENGTH, content_length_buffer);
    if (err != ERR_OK) {
        _DestroyHttpResponseRaw(raw_response);
        return ERR_HTTP_MEMORY;
    }

    // Date
    DynamicString *date = GetHttpDate(response->header.date);
    if (date == NULL) {
        _DestroyHttpResponseRaw(raw_response);
        return ERR_HTTP_MEMORY;
    }

    err = _AddHeader(raw_response, HTTP_HEADER_DATE, date->data);
    DestroyDynamicString(date);
    if (err != ERR_OK) {
        _DestroyHttpResponseRaw(raw_response);
        return ERR_HTTP_MEMORY;
    }

    // Last-Modified

    DynamicString *last_modified = GetHttpDate(response->header.last_modified);
    if (last_modified == NULL) {
        _DestroyHttpResponseRaw(raw_response);
        return ERR_HTTP_MEMORY;
    }
    err = _AddHeader(raw_response, HTTP_HEADER_LAST_MODIFIED, last_modified->data);
    DestroyDynamicString(last_modified);
    if (err != ERR_OK) {
        _DestroyHttpResponseRaw(raw_response);
        return ERR_HTTP_MEMORY;
    }

    // Final Delimiter
    err = AppendDynamicStringChar(raw_response->header_buffer, HTTP_HEADER_DELIMITER);
    if (err != ERR_OK) {
        _DestroyHttpResponseRaw(raw_response);
        return ERR_HTTP_MEMORY;
    }

    // Transfer body
    raw_response->body_buffer = response->body.body;
    response->body.body = NULL;

    raw_response->header_bytes_written = 0;
    raw_response->body_bytes_written = 0;

    if (request->raw_response) {
        _DestroyHttpResponseRaw(request->raw_response);
    }
    request->raw_response = raw_response;

    return ERR_OK;
}


int PrepareHttpResponseForbidden(HttpRequest *request) {
    HttpResponseRaw *raw_response = _CreateHttpResponseRaw();
    if (raw_response == NULL) {
        return ERR_HTTP_MEMORY;
    }

    LogDebugF("Preparing FORBIDDEN response for fd=%d", request->socketfd);

    // Write status line
    int err = _WriteStatusLine(raw_response, HTTP_ONE_DOT_ONE_VERSION, HTTP_FORBIDDEN_STATUS);
    if (err != ERR_OK) {
        _DestroyHttpResponseRaw(raw_response);
        return err;
    }

    // Final Delimiter
    err = AppendDynamicStringChar(raw_response->header_buffer, HTTP_HEADER_DELIMITER);
    if (err != ERR_OK) {
        _DestroyHttpResponseRaw(raw_response);
        return ERR_HTTP_MEMORY;
    }

    if (request->raw_response) {
        _DestroyHttpResponseRaw(request->raw_response);
    }
    request->raw_response = raw_response;

    return ERR_OK;
}

int PrepareHttpResponseNotFound(HttpRequest *request) {
    HttpResponseRaw *raw_response = _CreateHttpResponseRaw();
    if (raw_response == NULL) {
        return ERR_HTTP_MEMORY;
    }

    LogDebugF("Preparing NOT FOUND response for fd=%d", request->socketfd);

    // Write status line
    int err = _WriteStatusLine(raw_response, HTTP_ONE_DOT_ONE_VERSION, HTTP_NOT_FOUND_STATUS);
    if (err != ERR_OK) {
        _DestroyHttpResponseRaw(raw_response);
        return err;
    }

    // Final Delimiter
    err = AppendDynamicStringChar(raw_response->header_buffer, HTTP_HEADER_DELIMITER);
    if (err != ERR_OK) {
        _DestroyHttpResponseRaw(raw_response);
        return ERR_HTTP_MEMORY;
    }

    if (request->raw_response) {
        _DestroyHttpResponseRaw(request->raw_response);
    }
    request->raw_response = raw_response;
    
    return ERR_OK;
}

int PrepareHttpResponseUnsupportedMethod(HttpRequest *request) {
    HttpResponseRaw *raw_response = _CreateHttpResponseRaw();
    if (raw_response == NULL) {
        return ERR_HTTP_MEMORY;
    }

    // Write status line
    int err = _WriteStatusLine(raw_response, HTTP_ONE_DOT_ONE_VERSION, HTTP_UNSUPPORTED_METHOD_STATUS);
    if (err != ERR_OK) {
        _DestroyHttpResponseRaw(raw_response);
        return err;
    }

    // Final Delimiter
    err = AppendDynamicStringChar(raw_response->header_buffer, HTTP_HEADER_DELIMITER);
    if (err != ERR_OK) {
        _DestroyHttpResponseRaw(raw_response);
        return ERR_HTTP_MEMORY;
    }

    if (request->raw_response) {
        _DestroyHttpResponseRaw(request->raw_response);
    }
    request->raw_response = raw_response;
    
    return ERR_OK;
}

int _WriteStatusLine(HttpResponseRaw *response, const char *version, const char *status) {
    int err = SetDynamicStringChar(response->header_buffer, version);
    if (err != ERR_OK) {
        return err;
    }
    err = AppendDynamicStringChar(response->header_buffer, " ");
    if (err != ERR_OK) {
        return err;
    }
    err = AppendDynamicStringChar(response->header_buffer, status);
    if (err != ERR_OK) {
        return err;
    }
    err = AppendDynamicStringChar(response->header_buffer, HTTP_HEADER_DELIMITER);
    if (err != ERR_OK) {
        return err;
    }

    response->header_buffer = response->header_buffer;
    return ERR_OK;
}

int _AddHeader(HttpResponseRaw *response, const char *header, const char *value) {
    int err = AppendDynamicStringChar(response->header_buffer, header);
    if (err != ERR_OK) {
        return err;
    }
    err = AppendDynamicStringChar(response->header_buffer, value);
    if (err != ERR_OK) {
        return err;
    }
    err = AppendDynamicStringChar(response->header_buffer, HTTP_HEADER_DELIMITER);
    if (err != ERR_OK) {
        return err;
    }
    return ERR_OK;
}

int ResetRawRequest(HttpRequest *request) {
    if (request->raw_request == NULL) {
        return ERR_OK;
    }
    request->raw_request->request_buffer->size = 0;
    return ERR_OK;
}

int ReadRequest(HttpRequest *request) {
    LogDebug("Reading request data");
    size_t upto = request->raw_request->request_buffer->capacity - request->raw_request->request_buffer->size;
    if (upto == 0) {
        int err = ExpandDynamicString(request->raw_request->request_buffer, request->raw_request->request_buffer->capacity);
        if (err != ERR_OK) {
            LogError("Failed to expand request buffer");
            return ERR_HTTP_MEMORY;
        }
        upto = request->raw_request->request_buffer->capacity - request->raw_request->request_buffer->size;
    }
    char *to = request->raw_request->request_buffer->data + request->raw_request->request_buffer->size;

    ssize_t bytes_read = read(request->socketfd, to, upto);
    if (bytes_read == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            LogDebug("Read would block");
            return ERR_REQUEST_NONBLOCKED_ERROR;
        }
        LogErrorF("Read error: %s", strerror(errno));
        return ERR_REQUEST_READ_ERROR;
    }
    request->raw_request->request_buffer->size += bytes_read;

    if (strnstr(request->raw_request->request_buffer->data, "\r\n\r\n", request->raw_request->request_buffer->size) != NULL) {
        LogDebug("Request read complete");
        return ERR_REQUEST_READ_END;
    }

    return ERR_OK;
}

int AddPathPrefix(HttpRequest *request, const char *prefix) {
    LogDebugF("Adding path prefix: %s", prefix);
    if (!request->parsed_request) {
        LogDebug("Request not parsed");
        return ERR_REQUEST_NOT_PARSED;
    }
    if (PrefixDynamicStringChar(request->parsed_request->path, prefix)) {
        return ERR_HTTP_MEMORY;
    }
    return ERR_OK; 
}

int ReplacePath(HttpRequest *request, const char *path) {
    LogDebugF("Replacing path: %s", path);
    if (!request->parsed_request) {
        LogDebug("Request not parsed");
        return ERR_REQUEST_NOT_PARSED;
    }
    if (SetDynamicStringChar(request->parsed_request->path, path)) {
        return ERR_HTTP_MEMORY;
    }
    return ERR_OK; 
}

int WriteRequest(HttpRequest *request) {
    LogDebug("Writing response");
    if (!request->raw_response) {
        LogError("Response not prepared");
        return ERR_RESPONSE_NOT_FILLED;
    }

    HttpResponseRaw *raw_response = request->raw_response;
    if (raw_response->header_buffer == NULL) {
        LogError("Header buffer not set");
        return ERR_RESPONSE_NOT_FILLED;
    }

    size_t header_left = raw_response->header_buffer->size - raw_response->header_bytes_written;
    if (header_left > 0) {
        char *header_from = raw_response->header_buffer->data + raw_response->header_bytes_written;

        ssize_t bytes_written = write(request->socketfd, header_from, header_left);
        if (bytes_written == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                LogDebug("Write would block");
                return ERR_RESPONSE_NONBLOCKED_ERROR;
            }
            LogErrorF("Write error: %s", strerror(errno));
            return ERR_RESPONSE_WRITE_ERROR;
        }
        raw_response->header_bytes_written += bytes_written;
        return ERR_OK;
    }

    if (raw_response->body_buffer == NULL) {
        return ERR_RESPONSE_WRITE_END;
    }

    LockReadBuffer(raw_response->body_buffer);
    size_t body_left = *raw_response->body_buffer->used - raw_response->body_bytes_written;
    if (body_left > 0) {
        const char *body_from = raw_response->body_buffer->data + raw_response->body_bytes_written;

        ssize_t bytes_written = write(request->socketfd, body_from, body_left);
        if (bytes_written == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                LogDebug("Body write would block");
                UnlockReadBuffer(raw_response->body_buffer);
                return ERR_RESPONSE_NONBLOCKED_ERROR;
            }
            LogErrorF("Body write error: %s", strerror(errno));
            UnlockReadBuffer(raw_response->body_buffer);
            return ERR_RESPONSE_WRITE_ERROR;
        }
        raw_response->body_bytes_written += bytes_written;
    }
    UnlockReadBuffer(raw_response->body_buffer);

    if (body_left == 0) {
        LogDebug("Response write complete");
        return ERR_RESPONSE_WRITE_END;
    }

    return ERR_OK;
}