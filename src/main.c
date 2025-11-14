#include "server/request.h"
#include "server/worker.h"
#include "server/server.h"
#include "server/errors.h"
#include "utils/string.h"
#include "utils/strutils.h"

#include<stdio.h>
#include<string.h>
#include<stdlib.h>


int main(int argc, char **argv) {
    if (argc != 2) {
        printf("Usage: %s <port>\n", argv[0]);
        return 1;
    }
    int port = atoi(argv[1]);

    printf("Hello World!\n");

    ServerParams server_params;
    server_params.static_root = "data";
    server_params.port = port;
    server_params.max_cache_size = 1024 * 1024 * 1024;
    server_params.max_cache_size *= 4;
    server_params.max_cache_entries = 1024;
    server_params.max_cache_entry_size =  1024 *1024; 
    server_params.max_cache_entry_size *= 2048;
    server_params.reader_count = 4;
    server_params.max_requests = 1024;
    server_params.worker_count = 8;
    printf("Server params created\n");
    Server *server = CreateServer(&server_params);
    if (server == NULL) {
        return 1;
    }

    printf("Server started\n");
    StartServer(server);

    return 0;
}