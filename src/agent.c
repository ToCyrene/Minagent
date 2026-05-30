#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agent.h"
#include "net.h"
#include "json.h"
#include "tools.h"

int agent_init(agent_t *a, const char *model, const char *sys_prompt,
               const char *history_path, int max_turns)
{
    (void)a;
    (void)model;
    (void)sys_prompt;
    (void)history_path;
    (void)max_turns;
    return -1;
}

int agent_load_history(agent_t *a, const char *path)
{
    (void)a;
    (void)path;
    return -1;
}

int agent_save_history(agent_t *a, const char *path)
{
    (void)a;
    (void)path;
    return -1;
}

char *agent_chat(agent_t *a, const char *input)
{
    (void)a;
    (void)input;
    return NULL;
}

void agent_clear_history(agent_t *a)
{
    (void)a;
}

void agent_free(agent_t *a)
{
    (void)a;
}
