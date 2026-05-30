#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#include "net.h"

static size_t write_cb(char *data, size_t size, size_t nmemb, void *userp)
{
    http_t *h = userp;
    size_t total = size * nmemb;

    size_t needed = h->resp_len + total + 1;
    if (needed > h->resp_cap)
    {
        size_t newcap = h->resp_cap ? h->resp_cap : 4096;
        while (newcap < needed)
            newcap *= 2;
        char *tmp = realloc(h->resp_buf, newcap);
        if (!tmp)
            return 0;
        h->resp_buf = tmp;
        h->resp_cap = newcap;
    }

    memcpy(h->resp_buf + h->resp_len, data, total);
    h->resp_len += total;
    h->resp_buf[h->resp_len] = '\0';
    return total;
}

int http_init(http_t *h, const char *url, const char *api_key)
{
    memset(h, 0, sizeof(*h));

    if (url)
    {
        int n = snprintf(h->api_url, sizeof(h->api_url), "%s", url);
        if (n < 0 || (size_t)n >= sizeof(h->api_url))
            return -1;
    }
    if (api_key)
    {
        int n = snprintf(h->api_key, sizeof(h->api_key), "%s", api_key);
        if (n < 0 || (size_t)n >= sizeof(h->api_key))
            return -1;
    }

    CURL *curl = curl_easy_init();
    if (!curl)
        return -1;
    h->curl = curl;

    struct curl_slist *hs = NULL;
    hs = curl_slist_append(hs, "Content-Type: application/json");
    hs = curl_slist_append(hs, "Accept: application/json");
    if (h->api_key[0])
    {
        char auth[320];
        int n = snprintf(auth, sizeof(auth), "Authorization: Bearer %s", h->api_key);
        if (n > 0 && (size_t)n < sizeof(auth))
            hs = curl_slist_append(hs, auth);
    }
    h->headers = hs;

    curl_easy_setopt(curl, CURLOPT_URL, h->api_url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hs);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, h);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "minagent/0.1");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);

    return 0;
}

int http_post(http_t *h, const char *body, char **out, size_t *out_len)
{
    h->resp_len = 0;

    curl_easy_setopt(h->curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(h->curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));

    CURLcode res = curl_easy_perform(h->curl);

    if (out)
        *out = h->resp_buf;
    if (out_len)
        *out_len = h->resp_len;

    if (res != CURLE_OK)
        return -1;

    long http_code = 0;
    curl_easy_getinfo(h->curl, CURLINFO_RESPONSE_CODE, &http_code);

    return (http_code == 200) ? 0 : -1;
}

void http_free(http_t *h)
{
    if (!h)
        return;
    if (h->headers)
    {
        curl_slist_free_all(h->headers);
        h->headers = NULL;
    }
    if (h->curl)
    {
        curl_easy_cleanup(h->curl);
        h->curl = NULL;
    }
    free(h->resp_buf);
    h->resp_buf = NULL;
    h->resp_cap = 0;
    h->resp_len = 0;
}

void http_reset(http_t *h)
{
    if (!h)
        return;
    if (h->resp_cap > 65536)
    {
        free(h->resp_buf);
        h->resp_buf = NULL;
        h->resp_cap = 0;
    }
    h->resp_len = 0;
}
