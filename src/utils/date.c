#include "utils/date.h"

#include <time.h>
#include <stdio.h>

#define DATE_BUFFER_SIZE 64

DynamicString *GetHttpDate(time_t date) {
    DynamicString *result = CreateDynamicString(DATE_BUFFER_SIZE);
    if (result == NULL) {
        return NULL;
    }

    struct tm *timeinfo = gmtime(&date);
    if (timeinfo == NULL) {
        DestroyDynamicString(result);
        return NULL;
    }

    static const char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", 
                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    
    int written = snprintf(result->data, result->capacity,
                          "%s, %02d %s %04d %02d:%02d:%02d GMT",
                          days[timeinfo->tm_wday],
                          timeinfo->tm_mday,
                          months[timeinfo->tm_mon],
                          timeinfo->tm_year + 1900,
                          timeinfo->tm_hour,
                          timeinfo->tm_min,
                          timeinfo->tm_sec);
    if (written < 0 || written >= (int) result->capacity) {
        DestroyDynamicString(result);
        return NULL;
    }

    result->size = written;
    return result;
}