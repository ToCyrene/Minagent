#ifndef AGENT_H
#define AGENT_H

#include <stddef.h>

typedef enum {
    ROLE_SYSTEM,
    ROLE_USER,
    ROLE_ASSISTANT,
    ROLE_TOOL
} role_t;

typedef struct {
    role_t role;
    char  *content;
    char  *tool_call_id;
    char  *tool_name;
    char  *tool_args;
} msg_t;

typedef struct {
    msg_t  *msgs;
    int     msg_count;
    int     msg_cap;
    char   *model;
    char   *system_prompt;
    char   *history_path;
    int     max_turns;
} agent_t;

int agent_init(agent_t *a, const char *model, const char *sys_prompt,
               const char *history_path, int max_turns);
int agent_load_history(agent_t *a, const char *path);
int agent_save_history(agent_t *a, const char *path);
char *agent_chat(agent_t *a, const char *input);
void agent_clear_history(agent_t *a);
void agent_free(agent_t *a);

#endif
