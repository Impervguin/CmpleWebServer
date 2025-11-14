#include "server/request.h"
#include "server/worker.h"
#include "server/server.h"
#include "server/errors.h"
#include "utils/string.h"
#include "utils/strutils.h"
#include "utils/log.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stddef.h>
#include <signal.h>
#include <getopt.h>
#include <unistd.h>

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

size_t parse_size(const char *str) {
    char *end;
    long long num = strtoll(str, &end, 10);
    if (*end == '\0') return (size_t)num;
    if (strcasecmp(end, "k") == 0) return (size_t)num * 1024;
    if (strcasecmp(end, "m") == 0) return (size_t)num * 1024 * 1024;
    if (strcasecmp(end, "g") == 0) return (size_t)num * 1024LL * 1024 * 1024;
    return 0;
}

char* human_size(size_t size) {
    static char buf[32];
    if (size >= 1024LL * 1024 * 1024) {
        sprintf(buf, "%.1f g", (double)size / (1024.0 * 1024 * 1024));
    } else if (size >= 1024 * 1024) {
        sprintf(buf, "%.1f m", (double)size / (1024.0 * 1024));
    } else if (size >= 1024) {
        sprintf(buf, "%.1f k", (double)size / 1024.0);
    } else {
        sprintf(buf, "%zu B", size);
    }
    return buf;
}

int main(int argc, char **argv) {
    LogInit();

    // Check for --help
    if (argc > 1 && strcmp(argv[1], "--help") == 0) {
        printf("Usage: %s [options]\n", argv[0]);
        printf("Options:\n");
        printf("  -r <root>       Static root directory (default: data)\n");
        printf("  -p <port>       Port number (default: 8080)\n");
        printf("  -c <size>       Max cache size (e.g., 1024m, default: 4g)\n");
        printf("  -e <num>        Max cache entries (default: 1024)\n");
        printf("  -s <size>       Max cache entry size (e.g., 2g, default: 2g)\n");
        printf("  -a <num>        Number of async readers (default: 4)\n");
        printf("  -m <num>        Max requests per worker (default: 1024)\n");
        printf("  -w <num>        Number of workers (default: 8)\n");
        printf("  -h              Show this help\n");
        return 0;
    }

    // defaults
    char *static_root = "data";
    int port = 8080;
    size_t max_cache_size = 4LL * 1024 * 1024 * 1024;
    int max_cache_entries = 1024;
    size_t max_cache_entry_size = 2048LL * 1024 * 1024;
    int reader_count = 4;
    int max_requests = 1024;
    int worker_count = 8;

    int opt;
    while ((opt = getopt(argc, argv, "r:p:c:e:s:a:m:w:h")) != -1) {
        switch (opt) {
            case 'r':
                static_root = optarg;
                break;
            case 'p':
                port = atoi(optarg);
                break;
            case 'c':
                max_cache_size = parse_size(optarg);
                break;
            case 'e':
                max_cache_entries = atoi(optarg);
                break;
            case 's':
                max_cache_entry_size = parse_size(optarg);
                break;
            case 'a':
                reader_count = atoi(optarg);
                break;
            case 'm':
                max_requests = atoi(optarg);
                break;
            case 'w':
                worker_count = atoi(optarg);
                break;
            case 'h':
                printf("Usage: %s [options]\n", argv[0]);
                printf("Options:\n");
                printf("  -r <root>       Static root directory (default: data)\n");
                printf("  -p <port>       Port number (default: 8080)\n");
                printf("  -c <size>       Cache size (e.g., 1024m, default: 4.0 g)\n");
                printf("  -e <num>        Max cache entries (default: 1024)\n");
                printf("  -s <size>       Max cache entry size (e.g., 2g, default: 2.0 g)\n");
                printf("  -a <num>        Number of async readers (default: 4)\n");
                printf("  -m <num>        Max requests per worker (default: 1024)\n");
                printf("  -w <num>        Number of workers (default: 8)\n");
                printf("  -h              Show this help\n");
                return 0;
            default:
                fprintf(stderr, "Usage: %s [options]\n", argv[0]);
                return 1;
        }
    }

    // log parameters
    LogInfoF("Static root: %s", static_root);
    LogInfoF("Port: %d", port);
    LogInfoF("Cache size: %zu bytes (%s)", max_cache_size, human_size(max_cache_size));
    LogInfoF("Max cache entries: %d", max_cache_entries);
    LogInfoF("Max cache entry size: %zu bytes (%s)", max_cache_entry_size, human_size(max_cache_entry_size));
    LogInfoF("Reader count: %d", reader_count);
    LogInfoF("Max requests per worker: %d", max_requests);
    LogInfoF("Worker count: %d", worker_count);

    ServerParams server_params;
    server_params.static_root = static_root;
    server_params.port = port;
    server_params.max_cache_size = max_cache_size;
    server_params.max_cache_entries = max_cache_entries;
    server_params.max_cache_entry_size = max_cache_entry_size;
    server_params.reader_count = reader_count;
    server_params.max_requests = max_requests;
    server_params.worker_count = worker_count;

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