#ifndef NET_H
#define NET_H

#include <stddef.h>

typedef struct {
    char api_url[512];
    char api_key[256];
    void *curl;
    void *headers;
    char *resp_buf;
    size_t resp_cap;
    size_t resp_len;
} http_t;

int http_init(http_t *h, const char *url, const char *api_key);
int http_post(http_t *h, const char *body, char **out, size_t *out_len);
void http_free(http_t *h);
void http_reset(http_t *h);

#endif
