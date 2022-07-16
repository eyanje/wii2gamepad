#ifndef __W2G_UTIL_H
#define __W2G_UTIL_H

#include <stddef.h>

/*
 * c2 has `len` non-null characters
 */
int strmatch(const char *c1, const char *c2, size_t len);

#endif // __W2G_UTIL_H