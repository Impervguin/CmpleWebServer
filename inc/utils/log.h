#ifndef UTILS_LOG_H
#define UTILS_LOG_H

#include <stdarg.h>
#include <time.h>

// Log levels
typedef enum {
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR
} LogLevel;

// ANSI color codes
#define LOG_COLOR_RESET   "\x1b[0m"
#define LOG_COLOR_DEBUG   "\x1b[34m"  // Blue
#define LOG_COLOR_INFO    "\x1b[32m"  // Green
#define LOG_COLOR_WARN    "\x1b[33m"  // Yellow
#define LOG_COLOR_ERROR   "\x1b[31m"  // Red

void LogInit(void);
void log_debug(const char* func, const char* format, ...);
void log_info(const char* func, const char* format, ...);
void log_warn(const char* func, const char* format, ...);
void log_error(const char* func, const char* format, ...);

#define LogDebug(msg) log_debug(__func__, msg)
#define LogInfo(msg)  log_info(__func__, msg)
#define LogWarn(msg)  log_warn(__func__, msg)
#define LogError(msg) log_error(__func__, msg)

#define LogDebugF(format, ...) log_debug(__func__, format, __VA_ARGS__)
#define LogInfoF(format, ...)  log_info(__func__, format, __VA_ARGS__)
#define LogWarnF(format, ...)  log_warn(__func__, format, __VA_ARGS__)
#define LogErrorF(format, ...) log_error(__func__, format, __VA_ARGS__)

#endif // UTILS_LOG_H