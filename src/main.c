#include "server/request.h"

#include<stdio.h>
#include<string.h>


int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    HttpRequest *request = CreateHttpRequest(0);
    if (request == NULL) {
        return 1;
    }

    char *request_buffer = "GET /hello.html HTTP/1.1\r\n"
                           "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:78.0) Gecko/20100101 Firefox/78.0\r\n"
                           "Host: localhost:8080\r\n"
                           "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8\r\n"
                           "Accept-Language: en-US,en;q=0.5\r\n"
                           "Accept-Encoding: gzip, deflate\r\n"
                           "Connection: keep-alive\r\n"
                           "Upgrade-Insecure-Requests: 1\r\n"
                           "Cache-Control: max-age=0\r\n"
                           "\r\n";
    DynamicString *request_dyn_buffer = CreateDynamicString(strlen(request_buffer));
    if (request_dyn_buffer == NULL) {
        return 1;
    }
    SetDynamicStringChar(request_dyn_buffer, request_buffer);
    request->request_buffer = request_dyn_buffer;
    printf("Request: %s\n", request->request_buffer->data);

    int err = ParseHttpRequest(request);
    if (err != ERR_OK) {
        DestroyHttpRequest(request);
        return 1;
    }

    printf("Parsed request: %s\n", request->parsed_request.path->data);

    err = FillHttpResponseHeader(request, GetFileStat("testdata/test.txt"));
    if (err != ERR_OK) {
        DestroyHttpRequest(request);
        return 1;
    }

    err = PrepareHttpResponseHeader(request);
    if (err != ERR_OK) {
        DestroyHttpRequest(request);
        return 1;
    }

    printf("%s\n", request->response.header_buffer->data);
    printf("Path: %s\n", request->parsed_request.path->data);

    return 0;
}