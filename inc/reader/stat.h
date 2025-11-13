#ifndef STAT_H__
#define STAT_H__

#include <stddef.h>
#include <time.h>

typedef enum  {
    RegulatFile = 0,
    Directory = 1,
    Symlink = 2,
    Other = 3,
} FileType;

typedef struct {
    int error;
    size_t file_size;
    FileType type;

    time_t last_modified;
    time_t last_accessed;
    time_t created;
} FileStatResponse;

FileStatResponse GetFileStat(const char *path);
FileStatResponse GetFileStatFd(int fd);

#define ERR_OK 0
#define ERR_STAT_FILE_NOT_FOUND 1
#define ERR_STAT_FILE 2

#endif // STAT_H__`