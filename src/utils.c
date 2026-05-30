#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "utils.h"

char *utils_trim(char *s)
{
    char *end;

    while (isspace((unsigned char)*s))
        s++;

    if (*s == '\0')
        return s;

    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end))
        end--;

    *(end + 1) = '\0';
    return s;
}

int utils_escape_json(const char *in, char *out, size_t outlen)
{
    static const char *const esc[256] = {
        ['\n'] = "\\n", ['\r'] = "\\r", ['\t'] = "\\t",
        ['\\'] = "\\\\", ['"'] = "\\\"",
    };

    size_t pos = 0;

    for (const char *p = in; *p; p++)
    {
        unsigned char c = (unsigned char)*p;
        const char *e = esc[c];
        if (e)
        {
            if (pos + 2 >= outlen)
                return -1;
            out[pos++] = '\\';
            out[pos++] = e[1];
        }
        else
        {
            if (pos + 1 >= outlen)
                return -1;
            out[pos++] = c;
        }
    }

    out[pos] = '\0';
    return 0;
}

int utils_split_csv(const char *in, char ***out, int *count)
{
    *out = NULL;
    *count = 0;

    if (!in || !*in)
        return 0;

    int n = 0;
    for (const char *q = in; *q; )
    {
        while (*q && isspace((unsigned char)*q))
            q++;
        if (!*q)
            break;
        n++;
        while (*q && *q != ',')
            q++;
        if (*q)
            q++;
    }
    if (n == 0)
        return 0;

    char *buf = strdup(in);
    if (!buf)
        return -1;

    char **arr = malloc(sizeof(char *) * n);
    if (!arr)
    {
        free(buf);
        return -1;
    }

    int idx = 0;
    char *p = buf;
    while (*p && idx < n)
    {
        while (*p && isspace((unsigned char)*p))
            p++;
        if (!*p)
            break;

        char *start = p;
        while (*p && *p != ',')
            p++;

        char saved = *p;
        *p = '\0';

        char *trimmed = utils_trim(start);
        arr[idx] = strdup(trimmed);
        if (!arr[idx])
        {
            for (int i = 0; i < idx; i++)
                free(arr[i]);
            free(arr);
            free(buf);
            return -1;
        }
        idx++;

        if (saved == ',')
            p++;
    }

    free(buf);
    *out = arr;
    *count = idx;
    return 0;
}
