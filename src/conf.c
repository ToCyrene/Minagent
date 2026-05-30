#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "conf.h"
#include "utils.h"

static int parse_int(const char *s, int *out)
{
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0')
        return -1;
    *out = (int)v;
    return 0;
}

int conf_load(const char *path, agent_conf_t *conf)
{
    memset(conf, 0, sizeof(*conf));

    conf->max_turns = 8;
    conf->max_context_msgs = 64;
    conf->timeout = 30;
    conf->daemon = 0;

    FILE *f = fopen(path, "r");
    if (!f)
        return -1;

    char line[8192];
    while (fgets(line, sizeof(line), f))
    {
        char *p = utils_trim(line);
        if (*p == '#' || *p == '\0')
            continue;

        char *eq = strchr(p, '=');
        if (!eq)
            continue;

        *eq = '\0';
        char *key = utils_trim(p);
        char *val = utils_trim(eq + 1);

        if (*key == '\0' || *val == '\0')
            continue;

        if (strcmp(key, "api_url") == 0)
            snprintf(conf->api_url, sizeof(conf->api_url), "%s", val);
        else if (strcmp(key, "api_key") == 0)
            snprintf(conf->api_key, sizeof(conf->api_key), "%s", val);
        else if (strcmp(key, "model") == 0)
            snprintf(conf->model, sizeof(conf->model), "%s", val);
        else if (strcmp(key, "system_prompt") == 0)
            snprintf(conf->system_prompt, sizeof(conf->system_prompt), "%s", val);
        else if (strcmp(key, "history_file") == 0)
            snprintf(conf->history_file, sizeof(conf->history_file), "%s", val);
        else if (strcmp(key, "socket_path") == 0)
            snprintf(conf->socket_path, sizeof(conf->socket_path), "%s", val);
        else if (strcmp(key, "log_file") == 0)
            snprintf(conf->log_file, sizeof(conf->log_file), "%s", val);
        else if (strcmp(key, "max_turns") == 0)
            parse_int(val, &conf->max_turns);
        else if (strcmp(key, "max_context_msgs") == 0)
            parse_int(val, &conf->max_context_msgs);
        else if (strcmp(key, "timeout") == 0)
            parse_int(val, &conf->timeout);
        else if (strcmp(key, "daemon") == 0)
            conf->daemon = (strcmp(val, "true") == 0 || strcmp(val, "1") == 0);
        else if (strcmp(key, "allowed_dirs") == 0)
            utils_split_csv(val, &conf->allowed_dirs, &conf->allowed_dirs_count);
        else if (strcmp(key, "banned_cmds") == 0)
            utils_split_csv(val, &conf->banned_cmds, &conf->banned_cmds_count);
    }

    fclose(f);
    return 0;
}

void conf_print(const agent_conf_t *conf)
{
    printf("api_url:         %s\n", conf->api_url);
    printf("api_key:         %s\n", conf->api_key[0] ? "***" : "");
    printf("model:           %s\n", conf->model);
    printf("system_prompt:   %s\n", conf->system_prompt);
    printf("history_file:    %s\n", conf->history_file);
    printf("socket_path:     %s\n", conf->socket_path);
    printf("log_file:        %s\n", conf->log_file);
    printf("max_turns:       %d\n", conf->max_turns);
    printf("max_context_msgs:%d\n", conf->max_context_msgs);
    printf("timeout:         %d\n", conf->timeout);
    printf("daemon:          %s\n", conf->daemon ? "true" : "false");

    printf("allowed_dirs [%d]:", conf->allowed_dirs_count);
    for (int i = 0; i < conf->allowed_dirs_count; i++)
        printf(" %s", conf->allowed_dirs[i]);
    printf("\n");

    printf("banned_cmds [%d]:", conf->banned_cmds_count);
    for (int i = 0; i < conf->banned_cmds_count; i++)
        printf(" %s", conf->banned_cmds[i]);
    printf("\n");
}

#define FREE_STR_ARRAY(arr, cnt) do { \
    if (arr) { \
        for (int _i = 0; _i < cnt; _i++) free(arr[_i]); \
        free(arr); \
        arr = NULL; \
        cnt = 0; \
    } \
} while (0)

void conf_free(agent_conf_t *conf)
{
    FREE_STR_ARRAY(conf->allowed_dirs, conf->allowed_dirs_count);
    FREE_STR_ARRAY(conf->banned_cmds, conf->banned_cmds_count);
}
