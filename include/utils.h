#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>

char *utils_trim(char *s);
int   utils_escape_json(const char *in, char *out, size_t outlen);
int   utils_split_csv(const char *in, char ***out, int *count);

#endif
