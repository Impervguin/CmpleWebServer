#include "server/request.h"
#include "server/worker.h"
#include "server/server.h"
#include "server/errors.h"
#include "utils/string.h"
#include "utils/strutils.h"
#include "utils/log.h"

#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<signal.h>

Server *server;

void _SignalHandler(int signal) {
    switch (signal) {
        case SIGINT:
            LogInfo("SIGINT received");
            GracefullyShutdownServer(server);
            break;
        case SIGTERM:
            LogInfo("SIGTERM received");
            GracefullyShutdownServer(server);
            break;
        case SIGHUP:
            LogInfo("SIGHUP received");
            GracefullyShutdownServer(server);
            break;
        case SIGQUIT:
            LogInfo("SIGQUIT received");
            GracefullyShutdownServer(server);
            break;
        default:
            LogInfoF("Unknown signal %d received", signal);
            break;
    }
}

int main(int argc, char **argv) {
    LogInit();
    if (argc != 2) {
        printf("Usage: %s <port>\n", argv[0]);
        return 1;
    }
    int port = atoi(argv[1]);
    
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
    server = CreateServer(&server_params);
    if (server == NULL) {
        return 1;
    }

    // Set up signal handlers
    signal(SIGINT, _SignalHandler);
    signal(SIGTERM, _SignalHandler);
    signal(SIGHUP, _SignalHandler);
    signal(SIGQUIT, _SignalHandler);

    StartServer(server);

    LogInfo("Server shutdown complete");
    DestroyServer(server);

    return 0;
}