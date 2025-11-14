#include "utils/log.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static int log_initialized = 0;

static const char* get_level_string(LogLevel level) {
    switch (level) {
        case LOG_LEVEL_DEBUG: return "DEBUG";
        case LOG_LEVEL_INFO:  return "INFO";
        case LOG_LEVEL_WARN:  return "WARN";
        case LOG_LEVEL_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

static const char* get_level_color(LogLevel level) {
    switch (level) {
        case LOG_LEVEL_DEBUG: return LOG_COLOR_DEBUG;
        case LOG_LEVEL_INFO:  return LOG_COLOR_INFO;
        case LOG_LEVEL_WARN:  return LOG_COLOR_WARN;
        case LOG_LEVEL_ERROR: return LOG_COLOR_ERROR;
        default: return LOG_COLOR_RESET;
    }
}

void LogInit(void) {
    if (!log_initialized) {
        log_initialized = 1;
        // Any initialization if needed
    }
}

static void Log(LogLevel level, const char* func, const char* format, va_list args) {
    if (!log_initialized) {
        LogInit();
    }

    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    char timestamp[20];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

    const char* level_str = get_level_string(level);
    const char* color = get_level_color(level);

    fprintf(stderr, "%s[%s] [%s] %s: ", color, timestamp, level_str, func);
    fprintf(stderr, "%s", LOG_COLOR_RESET);
    vfprintf(stderr, format, args);
    fprintf(stderr, "%s\n", LOG_COLOR_RESET);
}

void log_debug(const char* func, const char* format, ...) {
    va_list args;
    va_start(args, format);
    Log(LOG_LEVEL_DEBUG, func, format, args);
    va_end(args);
}

void log_info(const char* func, const char* format, ...) {
    va_list args;
    va_start(args, format);
    Log(LOG_LEVEL_INFO, func, format, args);
    va_end(args);
}

void log_warn(const char* func, const char* format, ...) {
    va_list args;
    va_start(args, format);
    Log(LOG_LEVEL_WARN, func, format, args);
    va_end(args);
}

void log_error(const char* func, const char* format, ...) {
    va_list args;
    va_start(args, format);
    Log(LOG_LEVEL_ERROR, func, format, args);
    va_end(args);
}