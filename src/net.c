/* libcurl headers needed when implementing */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "net.h"

int http_init(http_t *h, const char *url, const char *api_key)
{
    (void)h;
    (void)url;
    (void)api_key;
    return -1;
}

int http_post(http_t *h, const char *body, char **out, size_t *out_len)
{
    (void)h;
    (void)body;
    (void)out;
    (void)out_len;
    return -1;
}

void http_free(http_t *h)
{
    (void)h;
}
