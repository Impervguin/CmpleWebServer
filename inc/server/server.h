#ifndef SERVER_H__
#define SERVER_H__

#include "server/worker.h"
#include "server/request.h"

typedef struct Server Server;

typedef struct {
    const char *static_root;
    u_int16_t port;

    size_t max_cache_size;
    size_t max_cache_entries;
    size_t max_cache_entry_size;

    size_t reader_count;

    size_t max_requests;
    size_t worker_count;
} ServerParams;

Server *CreateServer(const ServerParams *params);
void DestroyServer(Server *server);

int StartServer(Server *server);
int ShutdownServer(Server *server);
int GracefullyShutdownServer(Server *server);

#endif // SERVER_H__