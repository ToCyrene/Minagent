#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "json.h"
#include "agent.h"
#include "tools.h"

char *json_build_request(const char *model, const char *sys_prompt,
                         msg_t *msgs, int msg_count,
                         const tool_def_t *tools, int tool_count)
{
    (void)model;
    (void)sys_prompt;
    (void)msgs;
    (void)msg_count;
    (void)tools;
    (void)tool_count;
    return NULL;
}

int json_get_str(const char *json, const char *path, char *out, size_t outlen)
{
    (void)json;
    (void)path;
    (void)out;
    (void)outlen;
    return -1;
}

int json_get_int(const char *json, const char *path, int *out)
{
    (void)json;
    (void)path;
    (void)out;
    return -1;
}

int json_extract_tool_calls(const char *json, tool_call_t *calls, int max)
{
    (void)json;
    (void)calls;
    (void)max;
    return -1;
}

int json_load_history(const char *filepath, msg_t **msgs, int *count)
{
    (void)filepath;
    (void)msgs;
    (void)count;
    return -1;
}

int json_save_history(const char *filepath, msg_t *msgs, int count)
{
    (void)filepath;
    (void)msgs;
    (void)count;
    return -1;
}
