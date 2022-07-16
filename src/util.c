#include "util.h"

#include <stdbool.h>

#include <string.h>

inline int strmatch(const char *c1, const char *c2, size_t len) {
    if (strlen(c1) == len) {
        return !strncmp(c1, c2, len);
    }
    return false;
}