#ifndef CONF_H
#define CONF_H

#include <stddef.h>

typedef struct {
    char api_url[512];
    char api_key[256];
    char model[128];
    char system_prompt[4096];
    char history_file[512];
    char socket_path[256];
    char log_file[512];

    int max_turns;
    int timeout;
    int daemon;

    char **allowed_dirs;
    int    allowed_dirs_count;

    char **banned_cmds;
    int    banned_cmds_count;
} agent_conf_t;

int  conf_load(const char *path, agent_conf_t *conf);
void conf_print(const agent_conf_t *conf);
void conf_free(agent_conf_t *conf);

#endif
