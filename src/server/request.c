#define _GNU_SOURCE
#include "server/request.h"
#include "server/consts.h"
#include "server/errors.h"
#include "cache/cache.h"
#include "utils/date.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

HttpRequest *CreateHttpRequest(int socketfd) {
    HttpRequest *request = malloc(sizeof(HttpRequest));
    if (request == NULL) {
        return NULL;
    }
    memset(request, 0, sizeof(HttpRequest));
    request->socketfd = socketfd;
    request->state = HTTP_STATE_CONNECT;
    request->request_buffer = CreateDynamicString(INITITAL_REQUEST_BUFFER_SIZE);
    if (request->request_buffer == NULL) {
        free(request);
        return NULL;
    }

    request->parsed_request.host = NULL;

    return request;
}

void _DestroyParsedHttpRequest(ParsedHttpRequest *request) {
    if (request->path != NULL) {
        DestroyDynamicString(request->path);
    }
    if (request->user_agent != NULL) {
        DestroyDynamicString(request->user_agent);
    }
    if (request->host != NULL) {
        DestroyDynamicString(request->host);
    }
}

void _DestroyHttpResponse(HttpResponse *response) {
    if (response->header_buffer != NULL) {
        DestroyDynamicString(response->header_buffer);
    }
    if (response->body != NULL) {
        ReleaseBuffer(response->body);
    }
}

void DestroyHttpRequest(HttpRequest *request) {
    _DestroyParsedHttpRequest(&request->parsed_request);
    _DestroyHttpResponse(&request->response);

    if (request->request_buffer != NULL) {
        DestroyDynamicString(request->request_buffer);
    }
    free(request);
}

int  ParseHttpRequest(HttpRequest *request) {
    if (request->request_parsed) {
        return ERR_OK;
    }

    char *request_buffer = request->request_buffer->data;

    ParsedHttpRequest *parsed_request = &request->parsed_request;
    parsed_request->method = HTTP_REQUEST_UNSUPPORTED;
    DynamicString *method_buffer = NULL;

    char *rest = request_buffer;
    char *line;
    while ((line = strtok_r(rest, "\r\n", &rest)) != NULL) {
        if (strncmp(line, "GET", 3) == 0 || strncmp(line, "HEAD", 4) == 0) { // Save method, analyze later
            if (method_buffer != NULL) {
                DestroyDynamicString(method_buffer);
                return ERR_HTTP_PARSE;
            }
            method_buffer = CreateDynamicString(10);
            if (method_buffer == NULL) {
                return ERR_HTTP_MEMORY;
            }
            int err = SetDynamicStringChar(method_buffer, line);
            if (err != ERR_OK) {
                DestroyDynamicString(method_buffer);
                return err;
            }
        } else if (strncmp(line, "User-Agent: ", 12) == 0) {
            parsed_request->user_agent = CreateDynamicString(10);
            if (parsed_request->user_agent == NULL) {
                return ERR_HTTP_MEMORY;
            }
            int err = SetDynamicStringChar(parsed_request->user_agent, line + 12);
            if (err != ERR_OK) {
                DestroyDynamicString(parsed_request->user_agent);
                return err;
            }
        } else if (strncmp(line, "Host: ", 6) == 0) {
            parsed_request->host = CreateDynamicString(10);
            if (parsed_request->host == NULL) {
                return ERR_HTTP_MEMORY;
            }
            int err = SetDynamicStringChar(parsed_request->host, line + 6);
            if (err != ERR_OK) {
                DestroyDynamicString(parsed_request->host);
                return err;
            }
        };
    }
    // Analyze method
    if (method_buffer == NULL) {
        return ERR_HTTP_PARSE;
    }
    if (strncmp(method_buffer->data, "GET ", 4) == 0) {
        parsed_request->method = HTTP_REQUEST_GET;
        rest = method_buffer->data + 4;
    } else if (strncmp(method_buffer->data, "HEAD ", 5) == 0) {
        parsed_request->method = HTTP_REQUEST_HEAD;
        rest = method_buffer->data + 5;
    } else {
        DestroyDynamicString(method_buffer);
        return ERR_UNSUPPORTED_HTTP_METHOD;
    }

    // Path
    parsed_request->path = CreateDynamicString(10);
    if (parsed_request->path == NULL) {
        DestroyDynamicString(method_buffer);
        return ERR_HTTP_MEMORY;
    }

    char *path = NULL;
    path = strtok_r(rest, " ", &rest);
    if (path == NULL) {
        DestroyDynamicString(method_buffer);
        return ERR_HTTP_PARSE;
    }
    int err = SetDynamicStringChar(parsed_request->path, path);
    if (err != ERR_OK) {
        DestroyDynamicString(method_buffer);
        return err;
    }
    
    // Http version
    char *version = NULL;
    version = strtok_r(rest, " ", &rest);
    if (version == NULL) {
        DestroyDynamicString(method_buffer);
        return ERR_HTTP_PARSE;
    }
    if (strncmp(version, "HTTP/1.1", 7) != 0 && strncmp(version, "HTTP/1.0", 7) != 0) {
        DestroyDynamicString(method_buffer);
        return ERR_UNSUPPORTED_HTTP_VERSION;
    }

    DestroyDynamicString(method_buffer);
    request->request_parsed = true;
    return ERR_OK;
}

int FillHttpResponseHeader(HttpRequest *request, FileStatResponse stat) {
    if (!request->request_parsed) {
        return ERR_REQUEST_NOT_PARSED;
    }
    if (request->response.header_filled) {
        return ERR_OK;
    }
    HttpResponse *response = &request->response;
    response->header.content_type = GetContentType(request->parsed_request.path->data);
    response->header.date = time(NULL);
    response->header.last_modified = stat.last_modified;
    response->header.content_length = stat.file_size;
    response->header_filled = true;
    return ERR_OK;
}

int _WriteStatusLine(HttpResponse *response, const char *version, const char *status) {
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

int _AddHeader(HttpResponse *response, const char *header, const char *value) {
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

int PrepareHttpResponseHeader(HttpRequest *request) {
    if (!request->response.header_filled) {
        return ERR_RESPONSE_NOT_FILLED;
    }
    HttpResponse *response = &request->response;

    response->header_buffer = CreateDynamicString(INITITAL_REQUEST_BUFFER_SIZE);
    if (response->header_buffer == NULL) {
        return ERR_HTTP_MEMORY;
    }

    // Write status line
    int err = _WriteStatusLine(response, HTTP_ONE_DOT_ONE_VERSION, HTTP_OK_STATUS);
    if (err != ERR_OK) {
        return err;
    }

    // Add headers
    // Content-Type
    const char *content_type = GetContentTypeString(response->header.content_type);
    if (content_type == NULL) {
        return ERR_HTTP_PARSE;
    }
    err = _AddHeader(response, HTTP_HEADER_CONTENT_TYPE, content_type);
    if (err != ERR_OK) {
        return ERR_HTTP_MEMORY;
    }

    // Content-Length
    char content_length_buffer[16];
    int written = snprintf(content_length_buffer, sizeof(content_length_buffer), "%zu", response->header.content_length);
    if (written < 0 || written >= (int)sizeof(content_length_buffer)) {
        return ERR_HTTP_MEMORY;
    }
    err = _AddHeader(response, HTTP_HEADER_CONTENT_LENGTH, content_length_buffer);
    if (err != ERR_OK) {
        return ERR_HTTP_MEMORY;
    }

    // Date
    DynamicString *date = GetHttpDate(response->header.date);
    if (date == NULL) {
        DestroyDynamicString(response->header_buffer);
        return ERR_HTTP_MEMORY;
    }

    err = _AddHeader(response, HTTP_HEADER_DATE, date->data);
    DestroyDynamicString(date);
    if (err != ERR_OK) {
        return err;
    }

    // Last-Modified
    DynamicString *last_modified = GetHttpDate(response->header.last_modified);
    if (last_modified == NULL) {
        DestroyDynamicString(response->header_buffer);
        return ERR_HTTP_MEMORY;
    }
    err = _AddHeader(response, HTTP_HEADER_LAST_MODIFIED, last_modified->data);
    DestroyDynamicString(last_modified);
    if (err != ERR_OK) {
        return ERR_HTTP_MEMORY;
    }

    err = AppendDynamicStringChar(response->header_buffer, HTTP_HEADER_DELIMITER);
    if (err != ERR_OK) {
        return err;
    }

    return ERR_OK;
}

int PrepareHttpForbiddenResponse(HttpRequest *request) {
    HttpResponse *response = &request->response;

    response->header_buffer = CreateDynamicString(INITITAL_REQUEST_BUFFER_SIZE);
    if (response->header_buffer == NULL) {
        return ERR_HTTP_MEMORY;
    }

    int err = _WriteStatusLine(response, HTTP_ONE_DOT_ONE_VERSION, HTTP_FORBIDDEN_STATUS);
    if (err != ERR_OK) {
        return err;
    }
    return ERR_OK;
}

int PrepareHttpNotFoundResponse(HttpRequest *request) {
    HttpResponse *response = &request->response;

    response->header_buffer = CreateDynamicString(INITITAL_REQUEST_BUFFER_SIZE);
    if (response->header_buffer == NULL) {
        return ERR_HTTP_MEMORY;
    }

    int err = _WriteStatusLine(response, HTTP_ONE_DOT_ONE_VERSION, HTTP_NOT_FOUND_STATUS);
    if (err != ERR_OK) {
        return err;
    }
    return ERR_OK;
}

int PrepareHttpUnsupportedMethodResponse(HttpRequest *request) {
    HttpResponse *response = &request->response;

    response->header_buffer = CreateDynamicString(INITITAL_REQUEST_BUFFER_SIZE);
    if (response->header_buffer == NULL) {
        return ERR_HTTP_MEMORY;
    }

    int err = _WriteStatusLine(response, HTTP_ONE_DOT_ONE_VERSION, HTTP_UNSUPPORTED_METHOD_STATUS);
    if (err != ERR_OK) {
        return err;
    }
    return ERR_OK;
}


