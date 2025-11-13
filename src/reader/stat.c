#include <reader/stat.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>

FileStatResponse _MakeStatResponse(struct stat *sb) {
    FileStatResponse response;
    response.error = ERR_OK;
    response.file_size = sb->st_size;
    if (S_ISREG(sb->st_mode)) {
        response.type = RegulatFile;
    } else if (S_ISDIR(sb->st_mode)) {
        response.type = Directory;
    } else if (S_ISLNK(sb->st_mode)) {
        response.type = Symlink;
    } else {
        response.type = Other; 
    }
    return response;
}

FileStatResponse GetFileStat(const char *path) {
    FileStatResponse response;
    struct stat sb;
    if (stat(path, &sb)) {
        if (errno == ENOENT) {
            response.error = ERR_STAT_FILE_NOT_FOUND;
        } else {
            response.error = ERR_STAT_FILE;
        }
        return response;
    }

    return _MakeStatResponse(&sb);
}

FileStatResponse GetFileStatFd(int fd) {
    FileStatResponse response;
    struct stat sb;
    if (fstat(fd, &sb)) {
        if (errno == ENOENT) {
            response.error = ERR_STAT_FILE_NOT_FOUND;
        } else {
            response.error = ERR_STAT_FILE;
        }
        return response;
    }

    return _MakeStatResponse(&sb);
}