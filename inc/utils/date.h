#ifndef DATE_H__
#define DATE_H__

#include <time.h>
#include "utils/string.h"

DynamicString *GetHttpDate(time_t date);

#endif // DATE_H__